#include "serverdaemon.h"

#include "../common/audioengine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

#include <atomic>

namespace rr {

static std::atomic<bool> g_stopRequested{false};

void ServerDaemon::requestStop() { g_stopRequested.store(true); }

ServerDaemon::ServerDaemon(QObject *parent) : QObject(parent)
{
    m_station = new Station(this);
    connect(m_station, &Station::logMessage,    this, &ServerDaemon::onLog);
    connect(m_station, &Station::started,       this, &ServerDaemon::onStarted);
    connect(m_station, &Station::clientChanged, this, &ServerDaemon::onClientChanged);
    connect(m_station, &Station::radioState,    this, &ServerDaemon::onRadioState);
}

ServerDaemon::~ServerDaemon()
{
    stop();
    delete m_station;
    m_station = nullptr;
}

void ServerDaemon::report(const QString &line) const
{
    QTextStream out(stdout);
    out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        << "  " << line << Qt::endl;
}

void ServerDaemon::onLog(const QString &message) { report(message); }

void ServerDaemon::onStarted(bool ok, const QString &message)
{
    report(message);
    if (!ok) {
        report(tr("Nothing to serve, stopping."));
        QCoreApplication::exit(1);
    }
}

void ServerDaemon::onClientChanged(const QString &peer, bool connected,
                                   bool encrypted, const QString &codec)
{
    // The disconnection is already logged by the network core.
    if (connected)
        report(tr("Client %1 connected (%2, %3)")
                   .arg(peer, codec, encrypted ? tr("encrypted") : tr("clear")));
}

void ServerDaemon::onRadioState(const rr::Ft891State &state)
{
    // Without --verbose only the transmit switches are logged: the rest
    // would scroll five times a second for nothing.
    if (state.ptt != m_lastPtt) {
        m_lastPtt = state.ptt;
        report(state.ptt ? tr("TX") : tr("RX"));
    }
    if (state.radioOn != m_lastOn) m_lastOn = state.radioOn;
    if (!m_verbose || !state.radioOn) return;

    const QString line = QStringLiteral("%1 Hz  %2  %3").arg(state.freqA).arg(state.mode, state.memMode);
    if (line != m_lastLine) {
        m_lastLine = line;
        report(line);
    }
}

// What the remote operator has to type: shown at start-up.
void ServerDaemon::reportAddresses(quint16 tcpPort) const
{
    bool found = false;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) && !(flags & QNetworkInterface::IsRunning))
            continue;
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback()) continue;
            if (a.toString().startsWith(QLatin1String("169.254."))) continue;
            found = true;
            report(tr("Reachable at %1:%2 (%3)").arg(a.toString()).arg(tcpPort)
                       .arg(iface.humanReadableName()));
        }
    }
    if (!found) report(tr("No network address found — is this machine connected?"));
}

bool ServerDaemon::start(const QString &configPath, bool verbose)
{
    m_verbose = verbose;

    if (!configPath.isEmpty() && !QFile::exists(configPath)) {
        report(tr("Configuration file not found: %1").arg(configPath));
        return false;
    }
    QSettings *settings = configPath.isEmpty()
        ? new QSettings(settingsOrganisation(), settingsApplication(), this)
        : new QSettings(configPath, QSettings::IniFormat, this);
    report(tr("Configuration: %1").arg(settings->fileName()));

    if (!AudioEngine::initialiseLibrary()) {
        report(tr("PortAudio failed to start"));
        return false;
    }

    const StationConfig cfg = loadStationConfig(*settings);
    if (cfg.radio.catPort.isEmpty()) {
        report(tr("No CAT port configured (key catPort)."));
        return false;
    }
    if (cfg.server.password.isEmpty())
        report(tr("Warning: empty password — anyone reaching the port can key the radio."));
    report(tr("FT-891 on %1 at %2 baud").arg(cfg.radio.catPort).arg(cfg.radio.catBaud));
    report(tr("Audio in: %1").arg(cfg.audioIn.isEmpty() ? tr("system default") : cfg.audioIn));
    report(tr("Audio out: %1").arg(cfg.audioOut.isEmpty() ? tr("system default") : cfg.audioOut));

    QString err;
    if (!m_station->start(cfg, &err)) {
        report(err);
        report(tr("Run with --list-audio to see the device names."));
        return false;
    }
    reportAddresses(cfg.server.tcpPort);

    auto *stopTimer = new QTimer(this);
    connect(stopTimer, &QTimer::timeout, this, &ServerDaemon::checkStopRequest);
    stopTimer->start(200);
    return true;
}

void ServerDaemon::checkStopRequest()
{
    if (!g_stopRequested.load()) return;
    report(tr("Stopping."));
    QCoreApplication::quit();
}

void ServerDaemon::stop()
{
    if (m_station) m_station->stop();
}

} // namespace rr
