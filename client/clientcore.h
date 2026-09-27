// Client core: TCP control, UDP audio, adaptive jitter buffer.
//
// Derived from RemoteRig's ClientCore. The audio path is unchanged; the
// control vocabulary is the FT-891's: a state, a map of settings by CAT code,
// and generic commands that the server validates against the description.
#pragma once

#include <QObject>
#include <QHostAddress>
#include <QMap>
#include <QVariantMap>
#include "../common/protocol.h"
#include "../common/audiocodec.h"
#include "../common/audioengine.h"
#include "../common/speechproc.h"

class QTcpSocket;
class QUdpSocket;
class QTimer;

namespace rr {

// Accepts "14.074", "14074", "14 074 000" or "14.074 MHz". Below 1000 the
// value is read in MHz, below 100000 in kHz, above in Hz.
quint64 parseFrequency(const QString &text, bool *ok = nullptr);

struct ClientConfig {
    QString host        = QStringLiteral("192.168.1.10");
    quint16 tcpPort     = 7300;
    quint16 udpPort     = 0;      // 0: the one the server announces
    QString password;
    bool    encrypt     = false;
    QString codec       = QStringLiteral("opus");   // "opus" or "pcm"
    int     bitrate     = 48000;
    int     inputDevice  = -1;    // microphone or virtual cable; -1 = receive only
    int     outputDevice = -1;    // headphones or virtual cable
    int     monitorDevice = -1;   // optional second output (monitor)
    int     framesPerBuffer = 480;
    int     jitterMs    = 40;
    float   rxGain      = 1.0f;
    float   txGain      = 1.0f;
    SpeechSettings speech;   // shaping of the modulation
    bool    autoReconnect = true;
};

class ClientCore : public QObject {
    Q_OBJECT
public:
    explicit ClientCore(QObject *parent = nullptr);
    ~ClientCore() override;

    bool connected() const { return m_authenticated; }

public slots:
    void connectToStation(const rr::ClientConfig &cfg);
    void disconnectFromStation();
    void setPtt(bool on);
    // freq {hz, vfo}, mode {v}, set {code, v[], confirmed}, read {codes|group|all},
    // band {index}, clar {delta}, power {on}, cwmem {n} — see Ft891Controller.
    void command(const QString &name, const QVariantMap &args);
    void startTune();
    void sendMorse(const QString &text);
    void stopMorse();
    void setKeySpeed(int wpm);
    // expectReply false for a setting: the radio does not answer, and
    // waiting would cost a timeout for nothing.
    void setCodec(const QString &codec, int bitrate);
    void setGains(float rx, float tx);
    void setJitterMs(int ms);
    void setSpeechSettings(const rr::SpeechSettings &s);
    void setInputDevice(int deviceIndex);
    void setOutputDevice(int deviceIndex);
    void setAutoReconnect(bool on);

signals:
    void connectionChanged(bool up, const QString &message);
    void receiveOnly(bool on);   // no microphone: no transmission
    void stateChanged(const rr::Ft891State &st);
    // Settings by CAT code. full: the whole map, sent at connection time.
    void valuesChanged(const QVariantMap &values, bool full);
    void readProgress(int done, int total);
    // What the server said about itself: IARU region, version.
    void serverInfo(int region, const QString &version);
    void logMessage(const QString &msg);
    // A message the operator should see, not just find in the log.
    void notice(const QString &msg);
    // Countdown to the next attempt, and its number.
    void retryCountdown(int secondsLeft, int attempt);

    void statsUpdated(int rttMs, int lostPackets, int jitterQueueMs,
                      float rxLevel, float txLevel, float gainReductionDb,
                      bool rxClipped, bool txClipped);

private slots:
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpError();
    void onTcpDisconnected();
    void onUdpReadyRead();
    void onAudioTick();
    void onKeepalive();
    void onStatsTick();
    void onRetryTick();

private:
    void openLink();
    void shutdownLink();
    void scheduleRetry();
    void cancelRetry();
    void sendJson(const QJsonObject &o);
    void handleControl(const QJsonObject &o);
    void sendPttPacket(bool on);
    void teardown(const QString &why);

    ClientConfig m_cfg;
    QTcpSocket *m_sock = nullptr;
    QUdpSocket *m_udp  = nullptr;
    QTimer *m_audioTimer = nullptr;
    QTimer *m_keepTimer  = nullptr;
    QTimer *m_statsTimer = nullptr;
    QTimer *m_retryTimer = nullptr;

    AudioEngine m_audio;
    AudioCodec  m_encoder;   // microphone to station
    AudioCodec  m_decoder;   // station to headphones
    SpeechProcessor m_speech;

    QByteArray m_rxBuffer, m_masterKey, m_udpKey, m_pttToken;
    quint64 m_txCounter = 0, m_rxCounter = 0;
    bool    m_authenticated = false;
    bool    m_encrypted = false;
    bool    m_ptt = false;
    bool    m_rxOnly = false;
    quint32 m_session = 0;
    quint32 m_seqOut = 0, m_tsOut = 0;
    quint32 m_expectedSeq = 0;
    int     m_lost = 0;
    int     m_rttMs = 0;
    qint64  m_pingSentAt = 0;
    bool    m_prefilled = false;
    // The operator's intent, distinct from the link state: it decides
    // whether a lost link is recovered.
    bool    m_wantConnected = false;
    int     m_retrySeconds = 0;
    int     m_retryDelay = 1;
    int     m_retryAttempt = 0;
    QString m_serverVersion;

    QHostAddress m_serverAddr;
    quint16 m_serverUdpPort = 0;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::ClientConfig)
