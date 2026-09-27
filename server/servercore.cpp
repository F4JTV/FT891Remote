#include "servercore.h"

#include "../common/crypto.h"
#include "../common/ft891.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

#ifndef RR_VERSION
#define RR_VERSION "0.0.0"
#endif

namespace rr {

ServerCore::ServerCore(QObject *parent) : QObject(parent) {}
ServerCore::~ServerCore() { stop(); }

// =================================================================== life
void ServerCore::start(const rr::ServerConfig &cfg)
{
    stop();
    m_cfg = cfg;
    m_cfg.region = qBound(1, cfg.region, 3);

    if (!AudioEngine::initialiseLibrary()) {
        emit started(false, tr("PortAudio failed to start"));
        return;
    }

    m_audio.setCaptureGain(cfg.rxGain);
    m_audio.setPlaybackGain(cfg.txGain);

    if (!m_audio.startCapture(cfg.inputDevice, cfg.framesPerBuffer)) {
        emit started(false, tr("Audio input: %1").arg(m_audio.lastError()));
        return;
    }
    if (!m_audio.startPlayback(cfg.outputDevice, cfg.framesPerBuffer)) {
        m_audio.stopCapture();
        emit started(false, tr("Audio output: %1").arg(m_audio.lastError()));
        return;
    }
    m_audio.setPlaybackMuted(true);   // nothing to the radio unless transmitting

    m_encoder.setCodec(CODEC_PCM16);
    m_decoder.setCodec(CODEC_PCM16);

    m_tcp = new QTcpServer(this);
    connect(m_tcp, &QTcpServer::newConnection, this, &ServerCore::onNewConnection);
    if (!m_tcp->listen(QHostAddress::Any, cfg.tcpPort)) {
        m_audio.stopAll();
        delete m_tcp; m_tcp = nullptr;
        emit started(false, tr("TCP port %1 unavailable").arg(cfg.tcpPort));
        return;
    }

    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(QHostAddress::Any, cfg.udpPort)) {
        m_audio.stopAll();
        delete m_tcp; m_tcp = nullptr;
        delete m_udp; m_udp = nullptr;
        emit started(false, tr("UDP port %1 unavailable").arg(cfg.udpPort));
        return;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &ServerCore::onUdpReadyRead);

    m_audioTimer = new QTimer(this);
    m_audioTimer->setTimerType(Qt::PreciseTimer);
    connect(m_audioTimer, &QTimer::timeout, this, &ServerCore::onAudioTick);
    m_audioTimer->start(5);

    m_tailTimer = new QTimer(this);
    m_tailTimer->setSingleShot(true);
    connect(m_tailTimer, &QTimer::timeout, this, &ServerCore::onPttTail);

    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        emit statsUpdated(0, m_lostIn, m_audio.captureLevel(), m_audio.playbackLevel());
    });
    m_statsTimer->start(150);

    // Dead man. The client sends an audio datagram every ten milliseconds
    // while it transmits, and a ping every second: two seconds of silence
    // mean it is gone, and the radio must not stay on the air.
    m_lastHeard.start();
    m_watchdog = new QTimer(this);
    connect(m_watchdog, &QTimer::timeout, this, &ServerCore::checkClientAlive);
    m_watchdog->start(500);

    emit started(true, tr("Listening on TCP %1 / UDP %2").arg(cfg.tcpPort).arg(cfg.udpPort));
}

void ServerCore::stop()
{
    for (QTimer **t : {&m_audioTimer, &m_tailTimer, &m_statsTimer, &m_watchdog}) {
        if (*t) { (*t)->stop(); delete *t; *t = nullptr; }
    }

    if (m_tx) { m_tx = false; emit requestPtt(false); }
    if (m_cw) { m_cw = false; emit requestMorseStop(); }
    m_tuning = false;

    // abort() emits disconnected() at once, which would call back into
    // onTcpDisconnected() in the middle of this teardown.
    m_authenticated = false;

    if (m_sock) {
        QTcpSocket *s = m_sock;
        m_sock = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    if (m_tcp) {
        QTcpServer *t = m_tcp;
        m_tcp = nullptr;
        t->disconnect(this);
        t->close();
        t->deleteLater();
    }
    if (m_udp) {
        QUdpSocket *u = m_udp;
        m_udp = nullptr;
        u->disconnect(this);
        u->close();
        u->deleteLater();
    }

    m_audio.stopAll();
    m_encrypted = false;
    m_clientUdpPort = 0;
    emit stopped();
}

void ServerCore::setGains(float rx, float tx)
{
    m_cfg.rxGain = rx;
    m_cfg.txGain = tx;
    m_audio.setCaptureGain(rx);
    m_audio.setPlaybackGain(tx);
}

// ============================================================= connection
void ServerCore::onNewConnection()
{
    QTcpSocket *s = m_tcp->nextPendingConnection();
    if (!s) return;

    if (m_sock) {   // one operator at a time
        s->write(frameJson(QJsonObject{{"t", "error"},
                                       {"msg", tr("Station already in use")}}, {}, 0));
        s->flush();
        s->disconnectFromHost();
        s->deleteLater();
        return;
    }

    m_sock = s;
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_sock, &QTcpSocket::readyRead,    this, &ServerCore::onTcpReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, &ServerCore::onTcpDisconnected);

    m_rxBuffer.clear();
    m_authenticated = false;
    m_encrypted = false;
    m_txCounter = m_rxCounter = 0;
    m_seqOut = m_tsOut = 0;
    m_lastSeqIn = 0;
    m_lostIn = 0;
    m_salt        = randomBytes(16);
    m_serverNonce = randomBytes(16);
    m_session     = quint32(QRandomGenerator::system()->generate());
    m_lastHeard.restart();

    // The client checks "app" before it authenticates: a front panel for
    // an FT-891 has nothing to drive on another kind of station.
    sendJson(QJsonObject{{"t", "challenge"},
                         {"proto", int(kVersion)},
                         {"app",   QString::fromLatin1(kAppName)},
                         {"ver",   QStringLiteral(RR_VERSION)},
                         {"radio", QStringLiteral("FT-891")},
                         {"salt",  QString::fromLatin1(m_salt.toHex())},
                         {"nonce", QString::fromLatin1(m_serverNonce.toHex())},
                         {"opus",  AudioCodec::opusAvailable()}});

    emit logMessage(tr("Incoming connection from %1").arg(m_sock->peerAddress().toString()));
}

void ServerCore::onTcpDisconnected()
{
    if (m_tx) setTx(false);
    if (m_cw && m_morseBusy) emit requestMorseStop();
    emit clientChanged({}, false, false, {});
    emit logMessage(tr("Client disconnected"));
    if (m_sock) { m_sock->deleteLater(); m_sock = nullptr; }
    m_authenticated = false;
    m_clientUdpPort = 0;
}

void ServerCore::dropClient(const QString &why)
{
    emit logMessage(tr("Client rejected: %1").arg(why));
    if (m_sock) {
        sendJson(QJsonObject{{"t", "error"}, {"msg", why}});
        m_sock->flush();
        m_sock->disconnectFromHost();
    }
}

void ServerCore::sendJson(const QJsonObject &o)
{
    if (!m_sock) return;
    const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
    m_sock->write(frameJson(o, key, m_txCounter++));
}

void ServerCore::sendState()
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "state"}, {"s", m_state.toJson()}});
}

void ServerCore::notice(const QString &text)
{
    // The PTT arrives three times over UDP and once over TCP: one refusal,
    // not four.
    if (text == m_lastNotice && m_noticeClock.isValid() && m_noticeClock.elapsed() < 1500)
        return;
    m_lastNotice = text;
    m_noticeClock.start();
    emit logMessage(text);
    if (m_authenticated) sendJson(QJsonObject{{"t", "notice"}, {"s", text}});
}

void ServerCore::onTcpReadyRead()
{
    if (!m_sock) return;
    m_rxBuffer.append(m_sock->readAll());
    while (m_rxBuffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_rxBuffer.constData()));
        if (len > 65536) { dropClient(tr("Invalid control frame")); return; }
        if (quint32(m_rxBuffer.size()) < 4 + len) return;

        const QByteArray body = m_rxBuffer.mid(4, int(len));
        m_rxBuffer.remove(0, int(4 + len));

        QJsonObject o;
        const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
        if (!parseJsonFrame(body, key, m_rxCounter++, &o)) {
            dropClient(tr("Unreadable control frame"));
            return;
        }
        handleControl(o);
        if (!m_sock) return;
    }
}

void ServerCore::handleControl(const QJsonObject &o)
{
    // Any frame at all is a sign of life.
    m_lastHeard.restart();

    const QString t = o["t"].toString();

    if (t == "auth") {
        const QByteArray clientNonce = QByteArray::fromHex(o["nonce"].toString().toLatin1());
        const QByteArray mac = QByteArray::fromHex(o["mac"].toString().toLatin1());
        const QByteArray key = deriveKey(m_cfg.password, m_salt);
        const QByteArray expect = hmac(key, m_serverNonce + clientNonce);

        if (mac.size() != expect.size() || mac != expect) {
            dropClient(tr("Wrong password"));
            return;
        }

        const bool wantEnc = o["encrypt"].toBool();
        if (m_cfg.requireEncryption && !wantEnc) {
            dropClient(tr("The server requires encryption"));
            return;
        }

        m_masterKey = key;
        m_udpKey    = subKey(key, "udp");
        m_pttToken  = subKey(key, "ptt").left(kPttTokenLen);
        m_authenticated = true;

        const QString codecName = o["codec"].toString(QStringLiteral("pcm"));
        const Codec c = (codecName == "opus" && AudioCodec::opusAvailable()) ? CODEC_OPUS : CODEC_PCM16;
        const int bitrate = o["bitrate"].toInt(48000);
        m_encoder.setCodec(c, bitrate);
        m_decoder.setCodec(c, bitrate);

        // The answer still goes in clear; encryption starts after it. It
        // carries everything the client needs to draw the panel at once.
        sendJson(QJsonObject{
            {"t", "authOk"},
            {"session",  double(m_session)},
            {"udpPort",  int(m_cfg.udpPort)},
            {"encrypt",  wantEnc},
            {"codec",    c == CODEC_OPUS ? "opus" : "pcm"},
            {"pttToken", QString::fromLatin1(m_pttToken.toHex())},
            {"region",   m_cfg.region},
            {"state",    m_state.toJson()},
            {"vals",     QJsonObject::fromVariantMap(m_vals)}});

        m_encrypted = wantEnc;
        emit clientChanged(m_sock->peerAddress().toString(), true, m_encrypted,
                           c == CODEC_OPUS ? "Opus" : "PCM");
        emit logMessage(tr("Client authenticated (%1, %2)")
                            .arg(c == CODEC_OPUS ? "Opus" : "16-bit PCM",
                                 m_encrypted ? tr("encrypted") : tr("unencrypted")));
        return;
    }

    if (!m_authenticated) { dropClient(tr("Authentication required")); return; }

    if (t == "cmd") {
        handleCommand(o["c"].toString(), o);
        return;
    }

    if (t == "ping") {
        sendJson(QJsonObject{{"t", "pong"}, {"ts", o["ts"]}});
        return;
    }
}

// =============================================================== commands
void ServerCore::handleCommand(const QString &c, const QJsonObject &o)
{
    if (c == "ptt") {
        setTx(o["v"].toBool());
        return;
    }
    if (c == "tune") {
        startTuneCycle();
        return;
    }
    if (c == "cw") {
        const QString text = o["text"].toString().left(500);
        if (text.trimmed().isEmpty()) return;
        if (m_tx) {
            notice(tr("Release the PTT before sending CW"));
            return;
        }
        // A new message while one is being keyed is appended by the radio
        // controller; the local keyer queues it as well.
        m_cw = true;
        m_cwClock.start();
        m_state.cw = true;
        sendState();
        emit requestMorse(text);
        return;
    }
    if (c == "cwstop") {
        emit requestMorseStop();
        m_cw = false;
        m_morseBusy = false;
        m_state.cw = false;
        sendState();
        return;
    }
    if (c == "keyspd") {
        emit requestKeySpeed(qBound(4, o["wpm"].toInt(), 60));
        return;
    }
    if (c == "codec") {
        const Codec cc = (o["v"].toString() == "opus" && AudioCodec::opusAvailable())
                             ? CODEC_OPUS : CODEC_PCM16;
        const int br = o["bitrate"].toInt(48000);
        m_encoder.setCodec(cc, br);
        m_decoder.setCodec(cc, br);
        emit logMessage(tr("Codec switched to %1").arg(cc == CODEC_OPUS ? "Opus" : "PCM"));
        return;
    }
    if (c == "cwmem") {
        // Without "message", the radio's own setting of the memory decides.
        startKeyerMemory(o["n"].toInt(), o.contains(QStringLiteral("message")) ? (o["message"].toBool() ? 1 : 0) : -1);
        return;
    }

    QVariantMap args = o.toVariantMap();
    args.remove(QStringLiteral("t"));
    args.remove(QStringLiteral("c"));

    if (c == "set") {
        const QString code = args.value(QStringLiteral("code")).toString();
        const QString v0 = args.value(QStringLiteral("v")).toStringList().value(0);
        // MOX is a PTT: it goes through the path that watches it.
        if (code == QLatin1String("MOX") || code == QLatin1String("TX")) {
            setTx(!v0.isEmpty() && v0 != QLatin1String("0"));
            return;
        }
        // Start tuning: the same guards as the TUNE key.
        if (code == QLatin1String("TNR") && v0 == QLatin1String("2")) {
            startTuneCycle();
            return;
        }
        if (code == QLatin1String("SPEED")) {
            // Both keyers follow the speed: the radio's and the local one.
            emit requestKeySpeed(qBound(4, v0.toInt(), 60));
            return;
        }
        // Keyer memories (KY1-KY5) and paddle messages (MSG1-MSG5, KY6-KYA)
        // key the radio: they go through the same guards as the PTT.
        if (code.startsWith(QLatin1String("KY")) && code.size() == 3) {
            startKeyerMemory(code.mid(2).toInt(), 0);
            return;
        }
        if (code.startsWith(QLatin1String("MSG")) && code.size() == 4) {
            startKeyerMemory(code.mid(3).toInt(), 1);
            return;
        }
        if (code == QLatin1String("PS") && v0 == QLatin1String("0") && m_tx)
            setTx(false);
    }
    if (c == "power" && !args.value(QStringLiteral("on")).toBool() && m_tx)
        setTx(false);

    emit requestCommand(c, args);
}

void ServerCore::startTuneCycle()
{
    // Not while transmitting, and not twice at once.
    if (m_tx || m_tuning || m_cw) {
        notice(tr("Tuning refused: the radio is busy"));
        return;
    }
    if (!m_state.txAllowed) {
        notice(tr("Tuning refused: out of band"));
        return;
    }
    m_tuning = true;
    m_tuneClock.start();
    m_state.tuning = true;
    sendState();
    emit requestTune();
    emit logMessage(tr("Tune requested"));
}

void ServerCore::startKeyerMemory(int n, int message)
{
    if (n < 1 || n > 5) return;
    if (m_tx || m_tuning) {
        notice(tr("Keyer memory refused: the radio is busy"));
        return;
    }
    if (!m_state.txAllowed) {
        notice(tr("Keyer memory refused: out of band"));
        return;
    }
    // The radio keys itself: the server only knows it is done when the
    // radio has been back in receive for a while.
    m_cw = true;
    m_morseBusy = false;
    m_cwClock.start();
    m_state.cw = true;
    sendState();
    QVariantMap args{{QStringLiteral("n"), n}};
    if (message >= 0) args.insert(QStringLiteral("message"), message == 1);
    emit requestCommand(QStringLiteral("cwmem"), args);
}

// ================================================================== radio
void ServerCore::onRadioNotice(const QString &msg)
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "notice"}, {"s", msg}});
}

void ServerCore::onRadioValues(const QVariantMap &delta)
{
    for (auto it = delta.cbegin(); it != delta.cend(); ++it) m_vals.insert(it.key(), it.value());
    // Split changes the transmit frequency.
    if (delta.contains(QStringLiteral("SPL"))) {
        updateBandGuard();
        sendState();
    }
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "vals"}, {"v", QJsonObject::fromVariantMap(delta)}});
}

void ServerCore::onReadProgress(int done, int total)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "progress"}, {"done", done}, {"total", total}});
}

void ServerCore::onMorseBusy(bool busy)
{
    m_morseBusy = busy;
    if (busy) {
        m_cw = true;
        m_cwClock.start();
    } else {
        // The last element has left the keyer; the radio may still be in
        // its break-in delay. onRadioState ends the message once it is back
        // in receive.
        m_cwClock.start();
    }
    if (m_state.cw != m_cw) {
        m_state.cw = m_cw;
        sendState();
    }
}

void ServerCore::onRadioState(const rr::Ft891State &st)
{
    m_state = st;

    // The tuning cycle is over when the radio has dropped its PTT. The delay
    // keeps us from concluding before it has even raised it.
    if (m_tuning) {
        const bool settled = !st.ptt && m_tuneClock.elapsed() > 1500;
        if (settled || m_tuneClock.elapsed() > 15000 || !st.radioOn) {
            m_tuning = false;
            emit logMessage(tr("Tuning finished"));
        }
    }
    m_state.tuning = m_tuning;

    // Same reasoning for the keyer: the radio drops its PTT between words
    // with a short break-in delay, so a few seconds of receive are needed.
    if (m_cw && !m_morseBusy) {
        const bool settled = !st.ptt && m_cwClock.elapsed() > 3000;
        if (settled || m_cwClock.elapsed() > 180000 || !st.radioOn) m_cw = false;
    }
    m_state.cw = m_cw;

    updateBandGuard();

    sendState();
}

quint64 ServerCore::txFrequency() const
{
    const bool split = m_vals.value(QStringLiteral("SPL")).toString().left(1) != QLatin1String("0")
                       && m_vals.contains(QStringLiteral("SPL"));
    qint64 hz = qint64(split ? m_state.freqB : m_state.freqA);
    if (m_state.txClar) hz += m_state.clarOffset;
    return quint64(qMax<qint64>(0, hz));
}

void ServerCore::updateBandGuard()
{
    if (!m_cfg.enforceBandEdges || !m_state.radioOn || m_state.freqA == 0) {
        m_txAllowed = true;
        m_state.txAllowed = true;
        return;
    }
    const quint64 hz = txFrequency();
    // The emitted spectrum is judged, not the carrier: in USB it spreads
    // above the displayed frequency, in LSB below. At exactly 7.200 MHz
    // LSB stays inside 40 m where USB leaves it.
    const ft891::Span span = ft891::occupiedSpan(hz, m_state.mode);
    const bool inside = ft891::spanInside(span, ft891::txRanges(m_cfg.region));
    if (inside != m_txAllowed)
        emit logMessage(inside
            ? tr("Back inside a transmit range")
            : tr("Out of band in %1: emission would span %2 to %3 Hz")
                  .arg(m_state.mode).arg(span.low).arg(span.high));
    m_txAllowed = inside;
    m_state.txAllowed = inside;
    // Cut at once if the operator was transmitting when the edge was crossed.
    if (!inside && m_tx) setTx(false);
}

// ==================================================================== PTT
// Called twice a second while a client is connected.
void ServerCore::checkClientAlive()
{
    if (!m_sock || !m_authenticated) return;
    const qint64 silent = m_lastHeard.elapsed();

    // Transmission first: that is the immediate risk.
    if (m_tx && silent > 2000) {
        emit logMessage(tr("Client silent for %1 s — transmission stopped")
                            .arg(silent / 1000.0, 0, 'f', 1));
        setTx(false);
    }
    if (m_cw && m_morseBusy && silent > 5000) {
        emit logMessage(tr("Client silent — keyer message stopped"));
        emit requestMorseStop();
        m_cw = false;
        m_morseBusy = false;
    }

    // Then free the station, so that another client can take over.
    if (silent > 15000) {
        emit logMessage(tr("Client silent for %1 s — station released")
                            .arg(silent / 1000.0, 0, 'f', 0));
        dropClient(tr("No news from the client"));
    }
}

void ServerCore::setTx(bool on)
{
    // During a tuning cycle or a keyer message the radio is already keyed:
    // a client PTT on top would make it toggle between the two.
    if (on && (m_tuning || m_cw)) {
        notice(tr("PTT refused: the radio is tuning or keying"));
        return;
    }
    // Last line of defence: the client greys its button out already, but
    // nothing keeps another program from sending the command.
    if (on && !m_state.txAllowed) {
        notice(tr("Transmission refused: out of band"));
        return;
    }
    if (on && !m_state.radioOn) {
        notice(tr("Transmission refused: the radio does not answer"));
        return;
    }

    if (on) {
        if (m_tailTimer) m_tailTimer->stop();
        if (m_tx) return;
        m_tx = true;
        m_audio.setCaptureMuted(true);     // no receive audio while sending
        m_audio.flushPlayback();
        m_audio.setPlaybackMuted(false);   // modulation to the radio
        emit requestPtt(true);
    } else {
        if (!m_tx) return;
        // Let the queued audio play out before releasing the PTT.
        if (m_tailTimer) m_tailTimer->start(m_cfg.pttTailMs);
        else onPttTail();
    }
}

void ServerCore::onPttTail()
{
    m_tx = false;
    emit requestPtt(false);
    m_audio.setPlaybackMuted(true);
    m_audio.flushPlayback();
    m_audio.flushCapture();
    m_audio.setCaptureMuted(false);
}

// ================================================================== audio
void ServerCore::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(m_udp->pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        m_udp->readDatagram(dg.data(), dg.size(), &from, &fromPort);

        if (!m_authenticated) continue;

        PktHeader h{};
        QByteArray payload;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        if (!parsePacket(dg, key, &h, &payload)) continue;
        if (h.session != m_session) continue;

        // A valid datagram of this session: the client is alive. This is what
        // carries the watchdog during a transmission.
        m_lastHeard.restart();

        // The client's real address, as seen through its NAT.
        if (m_clientUdpPort != fromPort || m_clientAddr != from) {
            m_clientAddr = from;
            m_clientUdpPort = fromPort;
        }

        if (h.type == PKT_PTT) {
            if (payload.size() < kPttTokenLen + 1) continue;
            if (payload.left(kPttTokenLen) != m_pttToken) continue;   // anti-spoofing
            setTx(payload.at(kPttTokenLen) != 0);
            continue;
        }

        if (h.type == PKT_AUDIO) {
            if (!m_tx) continue;   // TX audio ignored outside transmission
            if (m_lastSeqIn && h.seq > m_lastSeqIn + 1)
                m_lostIn += int(h.seq - m_lastSeqIn - 1);
            if (h.seq <= m_lastSeqIn && m_lastSeqIn - h.seq < 100) continue;  // late
            m_lastSeqIn = h.seq;

            int16_t pcm[kFrameSamples];
            if (m_decoder.decode(payload, pcm))
                m_audio.pushPlayback(pcm, kFrameSamples);
        }
    }
}

void ServerCore::onAudioTick()
{
    if (!m_authenticated || m_clientUdpPort == 0) {
        m_audio.flushCapture();
        return;
    }
    if (m_tx) return;   // no receive audio while transmitting

    int16_t pcm[kFrameSamples];
    while (m_audio.capturedAvailable() >= size_t(kFrameSamples)) {
        m_audio.readCaptured(pcm, kFrameSamples);
        const QByteArray payload = m_encoder.encode(pcm);
        if (payload.isEmpty()) continue;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        const QByteArray dg = buildPacket(PKT_AUDIO, m_encoder.codec(), 0,
                                          m_session, m_seqOut++, m_tsOut,
                                          payload, key);
        m_tsOut += kFrameSamples;
        m_udp->writeDatagram(dg, m_clientAddr, m_clientUdpPort);
    }
}

} // namespace rr
