#include "clientcore.h"
#include "../common/crypto.h"
#include "../common/ft891.h"

#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QDateTime>
#include <QHostInfo>
#include <QRegularExpression>
#include <QtEndian>

namespace rr {

quint64 parseFrequency(const QString &text, bool *ok)
{
    return ft891::parseFrequency(text, ok);
}

ClientCore::ClientCore(QObject *parent) : QObject(parent) {}
ClientCore::~ClientCore() { disconnectFromStation(); }

void ClientCore::connectToStation(const ClientConfig &cfg)
{
    shutdownLink();
    cancelRetry();
    m_cfg = cfg;
    m_wantConnected = true;
    m_retryDelay = 1;
    m_retryAttempt = 0;
    openLink();
}

// Opens the link. Separate from connectToStation so that a reconnection
// attempt goes through exactly the same sequence.
void ClientCore::openLink()
{
    const ClientConfig &cfg = m_cfg;

    if (!AudioEngine::initialiseLibrary()) {
        emit connectionChanged(false, tr("PortAudio failed to start"));
        scheduleRetry();
        return;
    }

    m_audio.setCaptureGain(cfg.txGain);
    m_audio.setPlaybackGain(cfg.rxGain);
    if (!m_audio.startPlayback(cfg.outputDevice, cfg.framesPerBuffer)) {
        emit connectionChanged(false, tr("Audio output: %1").arg(m_audio.lastError()));
        scheduleRetry();
        return;
    }

    // Without a microphone the link is still useful for listening: connect
    // anyway and simply forbid transmission.
    m_rxOnly = (cfg.inputDevice < 0);
    if (!m_rxOnly && !m_audio.startCapture(cfg.inputDevice, cfg.framesPerBuffer)) {
        emit logMessage(tr("Microphone unavailable: %1").arg(m_audio.lastError()));
        m_rxOnly = true;
    }
    if (m_rxOnly)
        emit logMessage(tr("Receive only: no microphone, transmit is disabled"));
    m_audio.setCaptureMuted(true);   // the microphone only goes out in transmit

    m_encoder.setCodec(CODEC_PCM16);
    m_decoder.setCodec(CODEC_PCM16);
    m_speech.configure(AudioEngine::kAudioRate, cfg.speech);

    m_sock = new QTcpSocket(this);
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_sock, &QTcpSocket::connected,    this, &ClientCore::onTcpConnected);
    connect(m_sock, &QTcpSocket::readyRead,    this, &ClientCore::onTcpReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, &ClientCore::onTcpDisconnected);
    connect(m_sock, &QTcpSocket::errorOccurred, this, &ClientCore::onTcpError);

    m_rxBuffer.clear();
    m_txCounter = m_rxCounter = 0;
    m_seqOut = m_tsOut = 0;
    m_expectedSeq = 0;
    m_lost = 0;
    m_prefilled = false;

    emit logMessage(tr("Connecting to %1:%2…").arg(cfg.host).arg(cfg.tcpPort));
    m_sock->connectToHost(cfg.host, cfg.tcpPort);
}

void ClientCore::disconnectFromStation()
{
    m_wantConnected = false;
    cancelRetry();
    shutdownLink();
    // Announced like a lost link. Without it the client went on showing the
    // link up: the button kept offering to disconnect, and pressing it only
    // disconnected again — there was no way back to Connect.
    emit connectionChanged(false, tr("Disconnected"));
}

// Closes the link without touching the operator's intent: used for a lost
// link as well as for a wanted disconnection.
void ClientCore::shutdownLink()
{
    if (m_ptt) setPtt(false);

    // QAbstractSocket::abort() emits disconnected() at once, inside the call.
    // Unguarded, that signal calls back into this function, clears m_sock,
    // and the outer call resumes on a dead pointer. So the loop is cut first:
    // m_authenticated false disarms the handler, and the pointer is detached
    // before abort().
    m_authenticated = false;

    for (QTimer **t : {&m_audioTimer, &m_keepTimer, &m_statsTimer}) {
        if (*t) { (*t)->stop(); delete *t; *t = nullptr; }
    }

    if (m_sock) {
        QTcpSocket *s = m_sock;
        m_sock = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
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
    m_rxOnly = false;
    m_ptt = false;
    m_serverUdpPort = 0;
}

// Capped exponential back-off: no point hammering a server that is off, but a
// short outage must be recovered within a second.
void ClientCore::scheduleRetry()
{
    if (!m_wantConnected || !m_cfg.autoReconnect) return;

    ++m_retryAttempt;
    m_retrySeconds = m_retryDelay;
    m_retryDelay = qMin(m_retryDelay * 2, 30);

    if (!m_retryTimer) {
        m_retryTimer = new QTimer(this);
        connect(m_retryTimer, &QTimer::timeout, this, &ClientCore::onRetryTick);
    }
    m_retryTimer->start(1000);
    emit retryCountdown(m_retrySeconds, m_retryAttempt);
    emit logMessage(tr("Link lost, retrying in %1 s (attempt %2)")
                        .arg(m_retrySeconds).arg(m_retryAttempt));
}

void ClientCore::cancelRetry()
{
    if (m_retryTimer) m_retryTimer->stop();
    m_retrySeconds = 0;
    emit retryCountdown(0, 0);
}

void ClientCore::onRetryTick()
{
    if (--m_retrySeconds > 0) {
        emit retryCountdown(m_retrySeconds, m_retryAttempt);
        return;
    }
    m_retryTimer->stop();
    emit retryCountdown(0, m_retryAttempt);
    if (m_wantConnected) openLink();
}


void ClientCore::teardown(const QString &why)
{
    shutdownLink();
    emit connectionChanged(false, why);
    scheduleRetry();
}

// ------------------------------------------------------------------ handshake
void ClientCore::onTcpConnected()
{
    emit logMessage(tr("Control link established, authenticating…"));
}

void ClientCore::onTcpError()
{
    if (!m_sock) return;
    teardown(m_sock->errorString());
}

void ClientCore::onTcpDisconnected()
{
    if (m_authenticated) teardown(tr("The station closed the link"));
}

void ClientCore::sendJson(const QJsonObject &o)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
    m_sock->write(frameJson(o, key, m_txCounter++));
}

void ClientCore::onTcpReadyRead()
{
    m_rxBuffer.append(m_sock->readAll());
    while (m_rxBuffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_rxBuffer.constData()));
        if (len > 65536) { teardown(tr("Invalid control frame")); return; }
        if (quint32(m_rxBuffer.size()) < 4 + len) return;
        const QByteArray body = m_rxBuffer.mid(4, int(len));
        m_rxBuffer.remove(0, int(4 + len));

        QJsonObject o;
        const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
        if (!parseJsonFrame(body, key, m_rxCounter++, &o)) {
            teardown(tr("Unreadable control frame"));
            return;
        }
        handleControl(o);
    }
}

void ClientCore::handleControl(const QJsonObject &o)
{
    const QString t = o["t"].toString();

    if (t == "error") {
        teardown(o["msg"].toString(tr("Rejected by the station")));
        return;
    }

    if (t == "challenge") {
        // A front panel for an FT-891 has nothing to drive on another kind of
        // station: RemoteRig answers the same handshake, but not with "app".
        if (o["app"].toString() != QLatin1String(kAppName)) {
            m_wantConnected = false;
            teardown(tr("This is not an FT891Remote station"));
            return;
        }
        if (o["proto"].toInt() != int(kVersion)) {
            m_wantConnected = false;
            teardown(tr("Protocol version %1 on the station, %2 here: update both sides")
                         .arg(o["proto"].toInt()).arg(int(kVersion)));
            return;
        }
        m_serverVersion = o["ver"].toString();
        const QByteArray salt        = QByteArray::fromHex(o["salt"].toString().toLatin1());
        const QByteArray serverNonce = QByteArray::fromHex(o["nonce"].toString().toLatin1());
        const QByteArray clientNonce = randomBytes(16);
        m_masterKey = deriveKey(m_cfg.password, salt);

        QString codec = m_cfg.codec;
        if (codec == "opus" && (!o["opus"].toBool() || !AudioCodec::opusAvailable()))
            codec = "pcm";

        sendJson(QJsonObject{
            {"t", "auth"},
            {"nonce",   QString::fromLatin1(clientNonce.toHex())},
            {"mac",     QString::fromLatin1(hmac(m_masterKey, serverNonce + clientNonce).toHex())},
            {"encrypt", m_cfg.encrypt},
            {"codec",   codec},
            {"bitrate", m_cfg.bitrate}});
        return;
    }

    if (t == "authOk") {
        m_session       = quint32(o["session"].toDouble());
        // A forced port is for a NAT forwarding that exposes the audio on
        // another port than the one the server believes it uses.
        m_serverUdpPort = m_cfg.udpPort > 0 ? m_cfg.udpPort
                                            : quint16(o["udpPort"].toInt());
        m_pttToken      = QByteArray::fromHex(o["pttToken"].toString().toLatin1());
        m_udpKey        = subKey(m_masterKey, "udp");
        m_encrypted     = o["encrypt"].toBool();
        m_authenticated = true;

        const Codec c = (o["codec"].toString() == "opus") ? CODEC_OPUS : CODEC_PCM16;
        m_encoder.setCodec(c, m_cfg.bitrate);
        m_decoder.setCodec(c, m_cfg.bitrate);

        emit serverInfo(o["region"].toInt(1), m_serverVersion);
        emit stateChanged(Ft891State::fromJson(o["state"].toObject()));
        emit valuesChanged(o["vals"].toObject().toVariantMap(), true);

        m_serverAddr = m_sock->peerAddress();
        m_udp = new QUdpSocket(this);
        m_udp->bind(QHostAddress::AnyIPv4, 0);
        connect(m_udp, &QUdpSocket::readyRead, this, &ClientCore::onUdpReadyRead);

        m_audioTimer = new QTimer(this);
        m_audioTimer->setTimerType(Qt::PreciseTimer);
        connect(m_audioTimer, &QTimer::timeout, this, &ClientCore::onAudioTick);
        m_audioTimer->start(5);

        m_keepTimer = new QTimer(this);
        connect(m_keepTimer, &QTimer::timeout, this, &ClientCore::onKeepalive);
        m_keepTimer->start(1000);
        onKeepalive();   // opens the return path through the NAT at once

        m_statsTimer = new QTimer(this);
        connect(m_statsTimer, &QTimer::timeout, this, &ClientCore::onStatsTick);
        m_statsTimer->start(200);

        m_retryDelay = 1;
        if (m_retryAttempt > 0) {
            emit logMessage(tr("Link restored after %1 attempt(s)").arg(m_retryAttempt));
            m_retryAttempt = 0;
        }
        cancelRetry();

        emit connectionChanged(true, tr("Station connected (%1, %2)")
                                         .arg(c == CODEC_OPUS ? "Opus" : "16-bit PCM",
                                              m_encrypted ? tr("encrypted") : tr("unencrypted")));
        return;
    }

    if (t == "state") {
        emit stateChanged(Ft891State::fromJson(o["s"].toObject()));
        return;
    }

    if (t == "vals") {
        emit valuesChanged(o["v"].toObject().toVariantMap(), false);
        return;
    }

    if (t == "progress") {
        emit readProgress(o["done"].toInt(), o["total"].toInt());
        return;
    }

    if (t == "notice") {
        // For the operator: shown as it is.
        emit notice(o["s"].toString());
        emit logMessage(o["s"].toString());
        return;
    }


    if (t == "pong") {
        const qint64 sent = qint64(o["ts"].toDouble());
        m_rttMs = int(QDateTime::currentMSecsSinceEpoch() - sent);
        return;
    }
}

// ---------------------------------------------------------------- commands
void ClientCore::command(const QString &name, const QVariantMap &args)
{
    if (!m_authenticated) return;
    QJsonObject o = QJsonObject::fromVariantMap(args);
    o["t"] = QStringLiteral("cmd");
    o["c"] = name;
    sendJson(o);
}

void ClientCore::startTune()
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "cmd"}, {"c", "tune"}});
}

void ClientCore::sendMorse(const QString &text)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "cw"}, {"text", text}});
}

void ClientCore::stopMorse()
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "cmd"}, {"c", "cwstop"}});
}

void ClientCore::setKeySpeed(int wpm)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "keyspd"}, {"wpm", wpm}});
}

void ClientCore::setCodec(const QString &codec, int bitrate)
{
    if (!m_authenticated) return;
    const Codec c = (codec == "opus" && AudioCodec::opusAvailable()) ? CODEC_OPUS : CODEC_PCM16;
    sendJson(QJsonObject{{"t", "cmd"}, {"c", "codec"}, {"v", codec}, {"bitrate", bitrate}});
    m_encoder.setCodec(c, bitrate);
    m_decoder.setCodec(c, bitrate);
    m_cfg.codec = codec;
    m_cfg.bitrate = bitrate;
    m_prefilled = false;
    m_audio.flushPlayback();
}

void ClientCore::setGains(float rx, float tx)
{
    m_cfg.rxGain = rx; m_cfg.txGain = tx;
    m_audio.setPlaybackGain(rx);
    m_audio.setCaptureGain(tx);
}

void ClientCore::setJitterMs(int ms) { m_cfg.jitterMs = ms; }

void ClientCore::setAutoReconnect(bool on)
{
    m_cfg.autoReconnect = on;
    if (!on) cancelRetry();
}

// Microphone change while connected: the capture stream is closed and another
// opened. Network, codec and PTT are untouched, so the link holds; only the
// transmit audio pauses for a few tens of milliseconds.
void ClientCore::setInputDevice(int deviceIndex)
{
    m_cfg.inputDevice = deviceIndex;
    if (!m_authenticated) return;   // taken at the next connection

    m_audio.stopCapture();
    m_rxOnly = (deviceIndex < 0);
    if (m_rxOnly) {
        emit logMessage(tr("No microphone: receive only"));
        return;
    }

    if (!m_audio.startCapture(deviceIndex, m_cfg.framesPerBuffer)) {
        m_rxOnly = true;
        emit logMessage(tr("Audio input: %1").arg(m_audio.lastError()));
        return;
    }
    m_audio.setCaptureGain(m_cfg.txGain);
    // The microphone only goes out in transmit: follow the current PTT.
    m_audio.setCaptureMuted(!m_ptt);
    m_speech.reset();
    emit logMessage(tr("Microphone switched"));
}

void ClientCore::setOutputDevice(int deviceIndex)
{
    m_cfg.outputDevice = deviceIndex;
    if (!m_authenticated) return;

    m_audio.stopPlayback();
    if (deviceIndex < 0) {
        emit logMessage(tr("No playback device: nothing could be heard."));
        return;
    }

    if (!m_audio.startPlayback(deviceIndex, m_cfg.framesPerBuffer)) {
        emit logMessage(tr("Audio output: %1").arg(m_audio.lastError()));
        return;
    }
    m_audio.setPlaybackGain(m_cfg.rxGain);
    m_audio.setPlaybackMuted(m_ptt);
    // The jitter buffer starts again: it must fill before playing.
    m_prefilled = false;
    m_expectedSeq = 0;
    emit logMessage(tr("Playback switched"));
}

void ClientCore::setSpeechSettings(const rr::SpeechSettings &s)
{
    m_cfg.speech = s;
    m_speech.configure(AudioEngine::kAudioRate, s);
}

void ClientCore::sendPttPacket(bool on)
{
    if (!m_udp || !m_authenticated) return;
    QByteArray payload = m_pttToken;
    payload.append(char(on ? 1 : 0));
    const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
    const QByteArray dg = buildPacket(PKT_PTT, m_encoder.codec(), FLAG_TX,
                                      m_session, m_seqOut++, m_tsOut, payload, key);
    // Three copies: a lost datagram must never leave the radio keyed.
    for (int i = 0; i < 3; ++i)
        m_udp->writeDatagram(dg, m_serverAddr, m_serverUdpPort);
}

void ClientCore::setPtt(bool on)
{
    if (m_rxOnly) return;
    if (!m_authenticated || m_ptt == on) return;
    m_ptt = on;

    if (on) {
        // The PTT leaves before the audio, on both channels.
        sendPttPacket(true);
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "ptt"}, {"v", true}});
        m_speech.reset();   // filter states reset at every over
        m_audio.flushCapture();
        m_audio.setCaptureMuted(false);
        m_audio.setPlaybackMuted(true);
        m_audio.flushPlayback();
    } else {
        m_audio.setCaptureMuted(true);
        sendPttPacket(false);
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "ptt"}, {"v", false}});
        m_audio.setPlaybackMuted(false);
        m_prefilled = false;
    }
}

// -------------------------------------------------------------------- audio
void ClientCore::onKeepalive()
{
    if (!m_udp || !m_authenticated) return;
    const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
    const QByteArray dg = buildPacket(PKT_KEEPALIVE, m_encoder.codec(), FLAG_TX,
                                      m_session, m_seqOut++, m_tsOut, {}, key);
    m_udp->writeDatagram(dg, m_serverAddr, m_serverUdpPort);

    m_pingSentAt = QDateTime::currentMSecsSinceEpoch();
    sendJson(QJsonObject{{"t", "ping"}, {"ts", double(m_pingSentAt)}});
}

void ClientCore::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(m_udp->pendingDatagramSize()));
        m_udp->readDatagram(dg.data(), dg.size());

        PktHeader h{};
        QByteArray payload;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        if (!parsePacket(dg, key, &h, &payload)) continue;
        if (h.session != m_session || h.type != PKT_AUDIO) continue;
        if (m_ptt) continue;   // no listening while transmitting

        int16_t pcm[kFrameSamples];

        if (m_expectedSeq == 0) m_expectedSeq = h.seq;

        if (h.seq > m_expectedSeq) {
            // Gaps: filled by loss concealment rather than silence.
            const quint32 missing = h.seq - m_expectedSeq;
            if (missing < 20) {
                for (quint32 i = 0; i < missing; ++i) {
                    m_decoder.decode({}, pcm);
                    m_audio.pushPlayback(pcm, kFrameSamples);
                }
            }
            m_lost += int(missing);
        } else if (h.seq < m_expectedSeq) {
            continue;   // late packet: too late to be played
        }
        m_expectedSeq = h.seq + 1;

        if (m_decoder.decode(payload, pcm))
            m_audio.pushPlayback(pcm, kFrameSamples);

        // Start-up: wait for the requested buffer depth.
        if (!m_prefilled) {
            const size_t target = size_t(m_cfg.jitterMs) * kSampleRate / 1000;
            if (m_audio.playbackQueued() >= target) {
                m_prefilled = true;
                m_audio.setPlaybackMuted(false);
            } else {
                m_audio.setPlaybackMuted(true);
            }
        }
    }
}

void ClientCore::onAudioTick()
{
    if (!m_authenticated || !m_ptt) return;

    int16_t pcm[kFrameSamples];
    while (m_audio.capturedAvailable() >= size_t(kFrameSamples)) {
        m_audio.readCaptured(pcm, kFrameSamples);
        // Shaped before encoding: the codec gets a signal already rid of
        // useless bass.
        m_speech.process(pcm, kFrameSamples);
        const QByteArray payload = m_encoder.encode(pcm);
        if (payload.isEmpty()) continue;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        const QByteArray dg = buildPacket(PKT_AUDIO, m_encoder.codec(), FLAG_TX,
                                          m_session, m_seqOut++, m_tsOut, payload, key);
        m_tsOut += kFrameSamples;
        m_udp->writeDatagram(dg, m_serverAddr, m_serverUdpPort);
    }
}

void ClientCore::onStatsTick()
{
    const int queueMs = int(m_audio.playbackQueued() * 1000 / kSampleRate);
    // The clipping flags reset when read, so nothing accumulates.
    emit statsUpdated(m_rttMs, m_lost, queueMs,
                      m_audio.playbackLevel(), m_audio.captureLevel(),
                      m_ptt ? m_speech.gainReductionDb() : 0.0f,
                      m_audio.playbackClipped(), m_audio.captureClipped());
}

} // namespace rr
