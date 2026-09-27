#include "ft891link.h"

#include <QSerialPort>

namespace rr {

// The timers are given this object as parent so that they follow it when
// the controller moves to its own thread.
Ft891Link::Ft891Link(QObject *parent)
    : QObject(parent), m_timeout(this), m_gap(this)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &Ft891Link::onTimeout);
    m_gap.setSingleShot(true);
    connect(&m_gap, &QTimer::timeout, this, &Ft891Link::pump);
}

Ft891Link::~Ft891Link() { close(); }

bool Ft891Link::open(const QString &portName, int baud, QString *error)
{
    close();
    m_port = new QSerialPort(this);
    // QSerialPort takes the short name (« ttyUSB0 », « COM4 ») as well as a
    // full path, which the test harness uses for its pseudo-terminal.
    m_port->setPortName(portName);
    m_port->setBaudRate(baud);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    // Two stop bits are accepted by a receiver expecting one, never the
    // other way round; the FT-891 reference allows both.
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        if (error) *error = tr("cannot open %1: %2").arg(portName, m_port->errorString());
        delete m_port;
        m_port = nullptr;
        return false;
    }
    m_portName = portName;

    // Menu 05-08 CAT RTS makes the radio wait for RTS before it talks: keep
    // it asserted. The controller lowers it if the PTT is wired to it.
    m_port->setRequestToSend(true);
    m_port->setDataTerminalReady(false);
    m_port->clear();
    m_rx.clear();

    connect(m_port, &QSerialPort::readyRead, this, &Ft891Link::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        // A resource error is what a pulled USB cable looks like. Anything
        // else (a timeout, a parity glitch) is not worth tearing the link.
        if (e == QSerialPort::ResourceError || e == QSerialPort::PermissionError) {
            const QString why = m_port ? m_port->errorString() : QString();
            QTimer::singleShot(0, this, [this, why] {
                close();
                emit linkError(why);
            });
        }
    });
    return true;
}

void Ft891Link::close()
{
    m_timeout.stop();
    m_gap.stop();
    for (auto &q : m_queues) q.clear();
    m_hasInflight = false;
    m_rx.clear();
    if (m_port) {
        m_port->disconnect(this);
        // A last frame — the TX0; of a shutdown — must leave before the
        // port closes, or the radio would stay keyed.
        if (m_port->isOpen()) {
            if (m_port->bytesToWrite() > 0) m_port->waitForBytesWritten(200);
            m_port->close();
        }
        m_port->deleteLater();
        m_port = nullptr;
    }
}

bool Ft891Link::isOpen() const { return m_port && m_port->isOpen(); }

int Ft891Link::queuedTotal() const
{
    int n = 0;
    for (const auto &q : m_queues) n += q.size();
    return n;
}

bool Ft891Link::isQueued(const QString &key) const
{
    if (key.isEmpty()) return false;
    if (m_hasInflight && m_inflight.key == key) return true;
    for (const auto &q : m_queues)
        for (const Request &r : q)
            if (r.key == key) return true;
    return false;
}

void Ft891Link::dropQueue(Priority prio) { m_queues[prio].clear(); }

void Ft891Link::setRts(bool on) { if (isOpen()) m_port->setRequestToSend(on); }
void Ft891Link::setDtr(bool on) { if (isOpen()) m_port->setDataTerminalReady(on); }

// ------------------------------------------------------------------ writing
void Ft891Link::submit(const QByteArray &frame, const QByteArray &expect,
                       Priority prio, const QString &key, int timeoutMs)
{
    if (!isOpen() || frame.isEmpty()) return;

    if (!key.isEmpty()) {
        // Already being read: a second identical read would only repeat it.
        if (m_hasInflight && m_inflight.key == key && !expect.isEmpty()) return;
        // Still waiting: the newer request takes the older one's place.
        for (Request &r : m_queues[prio]) {
            if (r.key == key) {
                r.frame = frame;
                r.expect = expect;
                r.timeoutMs = timeoutMs;
                return;
            }
        }
    }
    m_queues[prio].append({frame, expect, key, timeoutMs});
    pump();
}

void Ft891Link::writeNow(const QByteArray &frame)
{
    if (!isOpen() || frame.isEmpty()) return;
    writeFrame(frame);
    m_lastSet = frame;
    m_lastSetClock.restart();
}

void Ft891Link::writeRaw(const QByteArray &bytes)
{
    if (!isOpen() || bytes.isEmpty()) return;
    m_port->write(bytes);
}

void Ft891Link::writeFrame(const QByteArray &frame)
{
    m_port->write(frame);
    emit frameSent(frame);
}

void Ft891Link::pump()
{
    if (!isOpen() || m_hasInflight || m_gap.isActive()) return;
    startNext();
}

void Ft891Link::startNext()
{
    for (auto &q : m_queues) {
        if (q.isEmpty()) continue;
        const Request r = q.takeFirst();
        writeFrame(r.frame);
        if (!r.expect.isEmpty()) {
            m_inflight = r;
            m_hasInflight = true;
            m_timeout.start(r.timeoutMs > 0 ? r.timeoutMs : m_readTimeoutMs);
        } else {
            // A setting has no answer. A short pause lets the radio digest
            // it before the next frame: a mode or band change takes it a
            // moment, and a frame arriving meanwhile can be lost.
            m_lastSet = r.frame;
            m_lastSetClock.restart();
            m_gap.start(m_gapMs);
        }
        return;
    }
}

void Ft891Link::completeInflight()
{
    m_timeout.stop();
    m_hasInflight = false;
    m_gap.start(0);
}

void Ft891Link::onTimeout()
{
    if (!m_hasInflight) return;
    const QByteArray frame = m_inflight.frame;
    m_hasInflight = false;
    emit requestFailed(frame, false);
    pump();
}

// ------------------------------------------------------------------ reading
void Ft891Link::onReadyRead()
{
    if (!m_port) return;
    m_rx.append(m_port->readAll());
    // A radio switched on mid-stream can emit noise without a terminator.
    if (m_rx.size() > 4096) m_rx.clear();

    int end;
    while ((end = m_rx.indexOf(';')) >= 0) {
        QByteArray raw = m_rx.left(end);
        m_rx.remove(0, end + 1);

        // Keep only from the first character a frame can start with: a
        // letter, or the « ? » of a refusal. Line noise, a stray NUL at
        // power-up, a CR sent by a terminal: all dropped here.
        int start = 0;
        while (start < raw.size()) {
            const char ch = raw.at(start);
            if ((ch >= 'A' && ch <= 'Z') || ch == '?') break;
            ++start;
        }
        raw = raw.mid(start).trimmed();
        if (!raw.isEmpty()) handleFrame(raw);
    }
}

void Ft891Link::handleFrame(const QByteArray &frame)
{
    if (frame == "?") {
        if (m_hasInflight) {
            const QByteArray f = m_inflight.frame;
            completeInflight();
            emit requestFailed(f, true);
        } else if (!m_lastSet.isEmpty() && m_lastSetClock.isValid()
                   && m_lastSetClock.elapsed() < 500) {
            emit requestFailed(m_lastSet, true);
        }
        return;
    }

    emit frameReceived(frame);
    if (m_hasInflight && frame.startsWith(m_inflight.expect))
        completeInflight();
}

} // namespace rr
