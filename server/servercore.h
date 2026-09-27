// Network side of the server: TCP control channel and UDP audio.
//
// Derived from RemoteRig's ServerCore. The audio path, the handshake, the
// dead-man watchdog and the PTT tail are unchanged. What changed is the
// control vocabulary: the client speaks FT-891 — settings by CAT code — and
// this class only guards what must be guarded (PTT, tuner cycle, keyer, band
// edges) before handing the rest to the radio controller.
//
// Lives in its own thread; the radio controller lives in another, so that a
// slow serial answer never delays an audio frame.
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QVariantMap>

#include "../common/audiocodec.h"
#include "../common/audioengine.h"
#include "../common/protocol.h"

class QTcpServer;
class QTcpSocket;
class QUdpSocket;
class QTimer;

namespace rr {

struct ServerConfig {
    quint16 tcpPort      = 7300;
    quint16 udpPort      = 7301;
    QString password     = QStringLiteral("changeme");
    bool    requireEncryption = false;   // otherwise the client decides
    int     inputDevice  = -1;           // audio from the radio (RX)
    int     outputDevice = -1;           // audio to the radio (TX)
    int     framesPerBuffer = 480;
    float   rxGain       = 1.0f;
    float   txGain       = 1.0f;
    int     pttTailMs    = 120;
    // A transverter works outside the radio's own ranges: the guard must be
    // switchable.
    bool    enforceBandEdges = true;
    int     region       = 1;            // IARU region, for the band edges
};

class ServerCore : public QObject {
    Q_OBJECT
public:
    explicit ServerCore(QObject *parent = nullptr);
    ~ServerCore() override;

    AudioEngine *audio() { return &m_audio; }

public slots:
    void start(const rr::ServerConfig &cfg);
    void stop();
    void setGains(float rx, float tx);

    // From the radio controller.
    void onRadioState(const rr::Ft891State &st);
    void onRadioValues(const QVariantMap &delta);
    void onReadProgress(int done, int total);
    void onRadioNotice(const QString &msg);
    // From whichever keyer is sending: the radio's memories or the local one.
    void onMorseBusy(bool busy);

signals:
    void started(bool ok, const QString &message);
    void stopped();
    void logMessage(const QString &msg);
    void clientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec);
    void statsUpdated(int rttMs, int lostPackets, float rxLevel, float txLevel);

    // To the radio controller (queued connections).
    void requestPtt(bool on);
    void requestCommand(const QString &name, const QVariantMap &args);
    void requestTune();
    void requestMorse(const QString &text);
    void requestMorseStop();
    void requestKeySpeed(int wpm);

private slots:
    void onNewConnection();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onUdpReadyRead();
    void onAudioTick();
    void onPttTail();
    void checkClientAlive();

private:
    void sendJson(const QJsonObject &o);
    void sendState();
    void notice(const QString &text);
    void handleControl(const QJsonObject &o);
    void handleCommand(const QString &c, const QJsonObject &o);
    void dropClient(const QString &why);
    void setTx(bool on);
    void startTuneCycle();
    // n 1-5; message 1: the paddle recording (KY6-KYA), 0: the text
    // (KY1-KY5), -1: as the memory is set in menus 04-07 to 04-11.
    void startKeyerMemory(int n, int message);
    void updateBandGuard();
    quint64 txFrequency() const;

    ServerConfig m_cfg;
    QTcpServer  *m_tcp = nullptr;
    QTcpSocket  *m_sock = nullptr;
    QUdpSocket  *m_udp = nullptr;
    QTimer      *m_audioTimer = nullptr;
    QTimer      *m_tailTimer  = nullptr;
    QTimer      *m_statsTimer = nullptr;
    QTimer      *m_watchdog   = nullptr;

    AudioEngine m_audio;
    AudioCodec  m_encoder;   // radio RX to client
    AudioCodec  m_decoder;   // client to radio TX

    QByteArray  m_rxBuffer;
    QByteArray  m_masterKey;
    QByteArray  m_udpKey;
    QByteArray  m_pttToken;
    QByteArray  m_salt, m_serverNonce;
    quint64     m_txCounter = 0, m_rxCounter = 0;
    bool        m_authenticated = false;
    bool        m_encrypted = false;
    quint32     m_session = 0;
    quint32     m_seqOut = 0;
    quint32     m_tsOut = 0;
    quint32     m_lastSeqIn = 0;
    int         m_lostIn = 0;

    QHostAddress m_clientAddr;
    quint16      m_clientUdpPort = 0;
    bool         m_tx = false;

    // The radio as last reported, plus every setting read so far. A client
    // that connects receives both at once.
    Ft891State   m_state;
    QVariantMap  m_vals;

    bool         m_tuning = false;
    bool         m_cw = false;           // keyer message in progress
    bool         m_morseBusy = false;    // the keyer itself says it is sending
    // Previous verdict of the band guard, to log crossings rather than
    // every poll.
    bool         m_txAllowed = true;

    // A client that vanishes without closing its connection — power cut,
    // phone switched off — would otherwise leave the radio transmitting and
    // the station locked.
    QElapsedTimer m_lastHeard;
    QElapsedTimer m_cwClock;
    QElapsedTimer m_tuneClock;
    QElapsedTimer m_noticeClock;
    QString       m_lastNotice;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::ServerConfig)
