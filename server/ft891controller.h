// Drives the FT-891 over its own CAT protocol — no Hamlib in between.
//
// Replaces RemoteRig's RigController. Where that class asked Hamlib for a
// handful of generic values, this one knows the radio: it polls what the front
// panel shows, keeps a copy of every setting it has read, and publishes both.
//
// Lives in its own thread. The serial link never blocks the network thread,
// and the network thread never delays a CAT frame.
#pragma once

#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../common/protocol.h"

class QTimer;
class QSerialPort;

namespace rr {

class Ft891Link;
struct CatCommand;

struct Ft891Config {
    enum PttMethod { PttCat, PttRts, PttDtr, PttNone };

    QString   catPort;                 // Enhanced COM port of the FT-891
    int       catBaud      = 38400;    // must match menu 05-06 CAT RATE
    PttMethod ptt          = PttCat;
    QString   pttPort;                 // RTS/DTR PTT: empty means the CAT port
    int       pollMs       = 200;
    int       readTimeoutMs = 400;
    bool      powerOnAtStart = false;
    bool      logTraffic   = false;    // every CAT frame in the log
};

class Ft891Controller : public QObject {
    Q_OBJECT
public:
    explicit Ft891Controller(QObject *parent = nullptr);
    ~Ft891Controller() override;


public slots:
    // Qualified types: moc registers the signature as written, and a queued
    // invokeMethod by name compares those strings.
    void open(const rr::Ft891Config &cfg);
    void close();
    void setPtt(bool on);

    // Everything the client asks of the radio, other than PTT and CW:
    //   freq {hz, vfo}      mode {v}          set {code, v[], confirmed}
    //   read {codes[] | group | all}         band {index}
    //   clar {delta}        power {on}        cwmem {n}
    void command(const QString &name, const QVariantMap &args);

    // A frame typed by the operator, sent as it is. The answer comes back
    // through catReply.
    void sendCatString(const QString &frame, bool expectReply);

    void sendMorse(const QString &text);
    void stopMorse();
    void setKeySpeed(int wpm);
    void startTune();

signals:
    void stateChanged(const rr::Ft891State &st);
    // Settings that changed since the last emission, by CAT code.
    void valuesChanged(const QVariantMap &delta);
    void readProgress(int done, int total);
    void catReply(const QString &reply);
    void logMessage(const QString &msg);
    void notice(const QString &msg);       // for the operator, on the client
    void opened(bool ok, const QString &message);
    void morseBusy(bool busy);

private slots:
    void onFrame(const QByteArray &frame);
    void onFailed(const QByteArray &frame, bool refused);
    void onLinkError(const QString &message);
    void onPollTick();
    void onFlush();
    void onMorseChunkDone();

private:
    bool openLink();
    void startup();
    void setRadioOn(bool on);
    void markDirty();
    void storeValue(const QString &code, const QString &value);
    void submitRead(const CatCommand &c, int prio, const QString &keyPrefix);
    void readBackAfter(const CatCommand &c);
    void readGroupNamed(const QString &group, bool bulk);
    void readAll();
    // Memories: MR for each channel, MW to write one, MC to recall it.
    void readMemories(bool bulk);
    void doMemoryWrite(const QVariantMap &args);
    void doMemoryRecall(const QString &channel);
    // Memories are read one at a time, and an answer is kept only if it
    // names the channel asked for: see readNextMemory().
    QStringList m_memQueue;
    QString     m_memInflight;
    int         m_memTries = 0;
    int         m_memGen = 0;       // invalidates a pending retry
    int         m_memBulkLeft = 0;  // memories still to read for Read all
    void queueMemory(const QString &channel, bool first);
    void readNextMemory();
    void sendMemoryRead();
    void finishMemoryRead(bool gaveUp = false);
    void retryMemoryRead(const QString &why);
    void doSet(const QVariantMap &args);
    void doFrequency(const QVariantMap &args);
    void doMode(const QString &name);
    void doBand(int index);
    void doClarifier(int delta);
    void powerOn();
    void powerOff();
    void applyLinePtt(bool on);
    void nextMorseChunk();
    void finishMorse();
    int  currentWpm() const;
    void updateProgress();

    Ft891Config  m_cfg;
    Ft891Link   *m_link = nullptr;
    QSerialPort *m_pttSerial = nullptr;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_flushTimer = nullptr;
    QTimer *m_reopenTimer = nullptr;
    QTimer *m_morseTimer = nullptr;
    QTimer *m_progressTimer = nullptr;

    Ft891State   m_state;
    Ft891State   m_published;
    bool         m_stateDirty = false;
    QHash<QString, QString> m_values;
    QVariantMap  m_delta;

    // Poll schedule.
    QStringList  m_panelCodes;        // read one per tick, in rotation
    int          m_panelPos = 0;
    int          m_tick = 0;
    int          m_txMeterPos = 0;
    int          m_ifFailures = 0;
    qint64       m_quietUntil = 0;    // no silence detection before this

    // Bulk reads: how many were queued, for the progress bar.
    int          m_bulkTotal = 0;

    // Band stack request waiting for its verdict: BS can be refused.
    int          m_pendingBand = -1;

    // Frames typed by the operator, waiting for their answer.
    QList<QByteArray> m_rawExpect;

    // Keyer.
    QStringList  m_morseChunks;
    bool         m_morseActive = false;
    // KR1; still to send after a stopped message switched the keyer off.
    bool         m_keyerRestorePending = false;
    // The mode the radio last used in each family: CW-L, DATA-L, AM-N… are
    // chosen again when the operator comes back to that family, as the
    // radio's MODE key does.
    QHash<QString, QString> m_lastModeOfFamily;
    void noteMode(const QString &mode);
    void readBackMode();
    int          m_wpm = 20;
    bool         m_pttWanted = false;
    bool         m_closing = false;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::Ft891Config)
