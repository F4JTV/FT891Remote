// What the QML front panel sees of the station.
//
// One object, registered as the singleton « Radio » of the « FT891Remote »
// module. It owns the network core (in its own thread), keeps the last state
// and every setting the server has read, and turns them into what a front
// panel shows: formatted frequencies, meters in units, labels instead of CAT
// codes.
//
// Tuning is relative and incremental (tune(steps)), so that a rotary knob —
// a mouse wheel today, a drawn dial or a USB encoder tomorrow — only has to
// report detents. The displayed frequency follows the knob at once; the
// radio's own reading takes over again once the knob has been still for a
// moment.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include "../../common/protocol.h"
#include "../clientcore.h"
#include "androidservice.h"

class QTimer;

namespace rr {

class RigctldServer;
struct CatCommand;

class Ft891Bridge : public QObject, public VolumePttSink {
    Q_OBJECT

    // ------------------------------------------------------------ link
    Q_PROPERTY(bool connected       READ connected      NOTIFY connectionChanged)
    Q_PROPERTY(QString statusText   READ statusText     NOTIFY connectionChanged)
    Q_PROPERTY(bool retrying        READ retrying       NOTIFY retryChanged)
    Q_PROPERTY(QString retryText    READ retryText      NOTIFY retryChanged)
    Q_PROPERTY(bool receiveOnly     READ receiveOnly    NOTIFY connectionChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY connectionChanged)
    Q_PROPERTY(int region           READ region         NOTIFY connectionChanged)

    // ----------------------------------------------------------- radio
    Q_PROPERTY(bool linkOpen    READ linkOpen    NOTIFY stateChanged)
    Q_PROPERTY(bool radioOn     READ radioOn     NOTIFY stateChanged)
    Q_PROPERTY(bool ptt         READ ptt         NOTIFY stateChanged)
    Q_PROPERTY(bool pttLocal    READ pttLocal    NOTIFY pttLocalChanged)
    Q_PROPERTY(bool tuning      READ tuning      NOTIFY stateChanged)
    Q_PROPERTY(bool cwBusy      READ cwBusy      NOTIFY stateChanged)
    Q_PROPERTY(bool txAllowed   READ txAllowed   NOTIFY stateChanged)
    Q_PROPERTY(bool hiSwr       READ hiSwr       NOTIFY stateChanged)
    Q_PROPERTY(QString radioStatus READ radioStatus NOTIFY stateChanged)

    Q_PROPERTY(double freqA      READ freqA      NOTIFY frequencyChanged)
    Q_PROPERTY(double freqB      READ freqB      NOTIFY stateChanged)
    Q_PROPERTY(QString freqText  READ freqText   NOTIFY frequencyChanged)
    Q_PROPERTY(QString freqBText READ freqBText  NOTIFY stateChanged)
    // The mode as the radio reports it (LSB, USB, CW-U…), and the family it
    // belongs to (SSB, CW…). The operator chooses among the families.
    Q_PROPERTY(QString mode      READ mode       NOTIFY stateChanged)
    Q_PROPERTY(QString modeFamily READ modeFamily NOTIFY stateChanged)
    Q_PROPERTY(QStringList modes READ modes      CONSTANT)
    Q_PROPERTY(QString memMode   READ memMode    NOTIFY stateChanged)
    Q_PROPERTY(int memChannel    READ memChannel NOTIFY stateChanged)
    // 012, P1L…: a PMS channel has no number.
    Q_PROPERTY(QString memName   READ memName    NOTIFY stateChanged)
    Q_PROPERTY(int clarOffset    READ clarOffset NOTIFY stateChanged)
    Q_PROPERTY(bool rxClar       READ rxClar     NOTIFY stateChanged)
    Q_PROPERTY(bool txClar       READ txClar     NOTIFY stateChanged)
    Q_PROPERTY(int rptShift      READ rptShift   NOTIFY stateChanged)
    Q_PROPERTY(bool split        READ split      NOTIFY valuesChanged)
    Q_PROPERTY(int bandIndex     READ bandIndex  NOTIFY frequencyChanged)
    Q_PROPERTY(QStringList bands READ bands      CONSTANT)

    // Meters: fraction of full scale for the bar, text in units.
    Q_PROPERTY(double sMeter      READ sMeter      NOTIFY stateChanged)
    Q_PROPERTY(QString sMeterText READ sMeterText  NOTIFY stateChanged)
    Q_PROPERTY(double poMeter     READ poMeter     NOTIFY stateChanged)
    Q_PROPERTY(QString poText     READ poText      NOTIFY stateChanged)
    Q_PROPERTY(double swrMeter    READ swrMeter    NOTIFY stateChanged)
    Q_PROPERTY(QString swrText    READ swrText     NOTIFY stateChanged)
    Q_PROPERTY(double alcMeter    READ alcMeter    NOTIFY stateChanged)
    Q_PROPERTY(double compMeter   READ compMeter   NOTIFY stateChanged)
    Q_PROPERTY(QString compText   READ compText    NOTIFY stateChanged)

    // --------------------------------------------------------- settings
    // Bumped whenever a setting changes. QML bindings read it alongside
    // value() so that they are re-evaluated.
    Q_PROPERTY(int revision   READ revision   NOTIFY valuesChanged)
    Q_PROPERTY(int readDone   READ readDone   NOTIFY readProgressChanged)
    Q_PROPERTY(int readTotal  READ readTotal  NOTIFY readProgressChanged)
    Q_PROPERTY(QStringList menuGroups READ menuGroups CONSTANT)
    // The step read from the radio, in hertz for the current mode: it
    // changes with either.
    Q_PROPERTY(QString widthText READ widthText NOTIFY widthChanged)

    // ------------------------------------------------------ memories
    Q_PROPERTY(QStringList memoryChannels READ memoryChannels CONSTANT)
    Q_PROPERTY(int memoriesUsed  READ memoriesUsed  NOTIFY valuesChanged)
    Q_PROPERTY(int memoriesRead  READ memoriesRead  NOTIFY valuesChanged)
    // Every mode, as a memory stores it (LSB, USB, CW-U…): the MODE key
    // offers families, a memory holds an exact mode.
    Q_PROPERTY(QStringList exactModes READ exactModes CONSTANT)
    Q_PROPERTY(bool widthAvailable READ widthAvailable NOTIFY widthChanged)

    // ----------------------------------------------------------- tuning
    Q_PROPERTY(QVariantList tuneSteps READ tuneSteps CONSTANT)
    Q_PROPERTY(int stepIndex  READ stepIndex  WRITE setStepIndex  NOTIFY stepChanged)
    Q_PROPERTY(int stepHz     READ stepHz     NOTIFY stepChanged)
    Q_PROPERTY(bool knobActive READ knobActive NOTIFY frequencyChanged)

    // --------------------------------------------------------------- CW
    Q_PROPERTY(int wpm           READ wpm    WRITE setWpm    NOTIFY cwChanged)
    Q_PROPERTY(QString myCall    READ myCall WRITE setMyCall NOTIFY cwChanged)
    Q_PROPERTY(QStringList cwMacros READ cwMacros NOTIFY cwChanged)

    // ------------------------------------------------------ connection
    Q_PROPERTY(QString host     READ host     WRITE setHost     NOTIFY settingsChanged)
    Q_PROPERTY(int port         READ port     WRITE setPort     NOTIFY settingsChanged)
    Q_PROPERTY(int udpPort      READ udpPort  WRITE setUdpPort  NOTIFY settingsChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY settingsChanged)
    Q_PROPERTY(bool encrypt     READ encrypt  WRITE setEncrypt  NOTIFY settingsChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY settingsChanged)
    Q_PROPERTY(QString codec    READ codec    WRITE setCodec    NOTIFY settingsChanged)
    Q_PROPERTY(int jitterTarget READ jitterTarget WRITE setJitterTarget NOTIFY settingsChanged)
    Q_PROPERTY(int speechPreset READ speechPreset WRITE setSpeechPreset NOTIFY settingsChanged)
    Q_PROPERTY(double rxGain    READ rxGain   WRITE setRxGain   NOTIFY settingsChanged)
    Q_PROPERTY(double txGain    READ txGain   WRITE setTxGain   NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList inputDevices  READ inputDevices  NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY devicesChanged)
    Q_PROPERTY(int inputDevice  READ inputDevice  WRITE setInputDevice  NOTIFY settingsChanged)
    Q_PROPERTY(int outputDevice READ outputDevice WRITE setOutputDevice NOTIFY settingsChanged)
    Q_PROPERTY(bool pttOnVolumeKey READ pttOnVolumeKey WRITE setPttOnVolumeKey NOTIFY settingsChanged)
    Q_PROPERTY(bool rigctldEnabled READ rigctldEnabled WRITE setRigctldEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int rigctldPort  READ rigctldPort CONSTANT)
    Q_PROPERTY(bool isAndroid   READ isAndroid   CONSTANT)
    Q_PROPERTY(int statusBarHeight READ statusBarHeight CONSTANT)
    Q_PROPERTY(QString version  READ version     CONSTANT)

    // ------------------------------------------------------------ stats
    Q_PROPERTY(int rttMs        READ rttMs      NOTIFY statsChanged)
    Q_PROPERTY(int lostFrames   READ lostFrames NOTIFY statsChanged)
    Q_PROPERTY(int jitterMs     READ jitterMs   NOTIFY statsChanged)
    Q_PROPERTY(double rxLevel   READ rxLevel    NOTIFY statsChanged)
    Q_PROPERTY(double txLevel   READ txLevel    NOTIFY statsChanged)
    Q_PROPERTY(double gainReductionDb READ gainReductionDb NOTIFY statsChanged)
    Q_PROPERTY(QString logText  READ logText    NOTIFY logChanged)

public:
    explicit Ft891Bridge(QObject *parent = nullptr);
    ~Ft891Bridge() override;

    // ----------------------------------------------------- connection
    Q_INVOKABLE void connectToStation();
    Q_INVOKABLE void disconnectFromStation();
    Q_INVOKABLE void refreshDevices();

    // --------------------------------------------------------- tuning
    // Relative tuning in detents of the current step: what a knob reports.
    Q_INVOKABLE void tune(int detents);
    // Relative tuning in hertz, for a knob with its own acceleration.
    Q_INVOKABLE void tuneHz(int hz);
    Q_INVOKABLE void setFrequency(double hz);
    Q_INVOKABLE void setFrequencyB(double hz);
    // « 14.074 », « 7074 », « 14 074 000 »: false if it cannot be read.
    Q_INVOKABLE bool enterFrequency(const QString &text, bool vfoB = false);
    Q_INVOKABLE void stepUp();
    Q_INVOKABLE void stepDown();
    // A family — SSB, CW, AM, FM, RTTY, DATA — or an exact mode.
    Q_INVOKABLE void setMode(const QString &name);
    Q_INVOKABLE void selectBand(int index);
    Q_INVOKABLE void bandUp();
    Q_INVOKABLE void bandDown();
    Q_INVOKABLE void clarifier(int hz);
    Q_INVOKABLE void selectMemory(int channel);
    Q_INVOKABLE void power(bool on);

    // ------------------------------------------------------- settings
    // Every setting is addressed by its code in data/cat/ft891.json.
    Q_INVOKABLE QVariantMap describe(const QString &code) const;
    // The codes that get a row of their own: a companion is shown in its
    // switch's row (PRC with PRC-LVL), not in one of its own.
    Q_INVOKABLE QStringList withoutCompanions(const QStringList &codes) const;
    Q_INVOKABLE QStringList groupCodes(const QString &group) const;
    Q_INVOKABLE bool hasValue(const QString &code) const;
    Q_INVOKABLE QString value(const QString &code) const;            // raw answer
    Q_INVOKABLE QString paramValue(const QString &code, int index) const;
    Q_INVOKABLE int paramInt(const QString &code, int index) const;
    Q_INVOKABLE int enumIndex(const QString &code, int index) const;
    Q_INVOKABLE bool isOn(const QString &code) const;
    Q_INVOKABLE QString display(const QString &code) const;           // labels and units
    Q_INVOKABLE QString describeStep(const QString &code, int index, int step) const;
    // The range a slider offers now: { min, max, step, available, reason }.
    // Static for most settings; WIDTH follows the mode and NARROW.
    Q_INVOKABLE QVariantMap paramRange(const QString &code, int index) const;
    // Where the slider stands for the radio's value. WIDTH step 00, the
    // default, stands at the step giving the same bandwidth.
    Q_INVOKABLE int paramPosition(const QString &code, int index) const;
    // The radio's current value, as the slider's label shows it at rest. A
    // WIDTH step the table does not have for this mode stays a step number:
    // the slider cannot stand on it, and must not pretend to.
    Q_INVOKABLE QString describeCurrent(const QString &code, int index) const;
    Q_INVOKABLE void setParam(const QString &code, int index, const QString &value,
                              bool confirmed = false);
    Q_INVOKABLE void setEnumIndex(const QString &code, int index, int valueIndex,
                                  bool confirmed = false);
    Q_INVOKABLE void toggle(const QString &code);
    // An ON/OFF parameter set with its own values: ML0 takes 000/001,
    // CO00 0000/0001 — a bare 0 or 1 is not one of them.
    Q_INVOKABLE void setSwitch(const QString &code, int index, bool on, bool confirmed = false);
    Q_INVOKABLE bool isOnAt(const QString &code, int index) const;
    Q_INVOKABLE void run(const QString &code, bool confirmed = false);
    Q_INVOKABLE void readCodes(const QStringList &codes);
    Q_INVOKABLE void readGroup(const QString &group);
    Q_INVOKABLE void readAll();
    Q_INVOKABLE bool groupComplete(const QString &group) const;

    // --------------------------------------------------- PTT, tune, CW
    Q_INVOKABLE void setPtt(bool on);
    Q_INVOKABLE void startTune();
    Q_INVOKABLE void sendCw(const QString &text);
    Q_INVOKABLE void stopCw();
    // ------------------------------------------------------ memories
    // { ch, known, empty, hz, freqText, mode, clarOn, clarOffset, tone, shift }
    Q_INVOKABLE QVariantMap memory(const QString &ch) const;
    // VFO-A as a memory would hold it: to store where the radio is.
    Q_INVOKABLE QVariantMap vfoAsMemory() const;
    Q_INVOKABLE void readMemories();
    // { ch, hz, mode, clarOn, clarOffset, tone, shift } — false if invalid.
    Q_INVOKABLE bool writeMemory(const QVariantMap &m);
    Q_INVOKABLE void recallMemory(const QString &ch);
    // 0 when the text is not a frequency the FT-891 covers.
    Q_INVOKABLE double frequencyFromText(const QString &text) const;
    Q_INVOKABLE QString frequencyText(double hz) const { return formatFreq(quint64(hz)); }

    Q_INVOKABLE void playCwMemory(int n);    // text memory n (KY1-KY5)
    Q_INVOKABLE void playCwMessage(int n);   // paddle message n (KY6-KYA)
    Q_INVOKABLE void setCwMacro(int index, const QString &text);
    Q_INVOKABLE QString expandMacro(const QString &text) const;

    // ------------------------------------------------------------ CAT
    Q_INVOKABLE void clearLog();

    // ----------------------------------------------------- properties
    bool connected() const { return m_connected; }
    QString statusText() const { return m_status; }
    bool retrying() const { return m_retrySeconds > 0; }
    QString retryText() const;
    bool receiveOnly() const { return m_rxOnly; }
    QString serverVersion() const { return m_serverVersion; }
    int region() const { return m_region; }

    bool linkOpen() const { return m_state.linkOpen; }
    bool radioOn() const { return m_state.radioOn; }
    bool ptt() const { return m_state.ptt; }
    bool pttLocal() const { return m_pttLocal; }
    bool tuning() const { return m_state.tuning; }
    bool cwBusy() const { return m_state.cw; }
    bool txAllowed() const { return m_state.txAllowed; }
    bool hiSwr() const { return m_state.hiSwr; }
    QString radioStatus() const;

    double freqA() const { return double(displayedFreq()); }
    double freqB() const { return double(m_state.freqB); }
    QString freqText() const { return formatFreq(displayedFreq()); }
    QString freqBText() const { return formatFreq(m_state.freqB); }
    QString mode() const { return m_state.mode; }
    QStringList modes() const;
    QString modeFamily() const;
    QString memMode() const { return m_state.memMode; }
    int memChannel() const { return m_state.memChannel; }
    QString memName() const { return m_state.memName.isEmpty() ? QString::number(m_state.memChannel) : m_state.memName; }
    int clarOffset() const { return m_state.clarOffset; }
    bool rxClar() const { return m_state.rxClar; }
    bool txClar() const { return m_state.txClar; }
    int rptShift() const { return m_state.rptShift; }
    bool split() const;
    int bandIndex() const;
    QStringList bands() const;

    double sMeter() const { return m_state.sMeter / 255.0; }
    QString sMeterText() const;
    double poMeter() const;
    QString poText() const;
    double swrMeter() const;
    QString swrText() const;
    double alcMeter() const { return m_state.alc / 255.0; }
    double compMeter() const { return m_state.comp / 255.0; }
    QString compText() const;

    int revision() const { return m_revision; }
    int readDone() const { return m_readDone; }
    int readTotal() const { return m_readTotal; }
    QStringList menuGroups() const;
    QString widthText() const;
    QStringList memoryChannels() const;
    int memoriesUsed() const;
    int memoriesRead() const;
    QStringList exactModes() const;
    bool widthAvailable() const;
    bool narrowOn() const;

    QVariantList tuneSteps() const;
    int stepIndex() const { return m_stepIndex; }
    void setStepIndex(int i);
    int stepHz() const;
    bool knobActive() const;

    int wpm() const { return m_wpm; }
    void setWpm(int v);
    QString myCall() const { return m_myCall; }
    void setMyCall(const QString &v);
    QStringList cwMacros() const { return m_macros; }

    QString host() const { return m_cfg.host; }
    void setHost(const QString &v);
    int port() const { return m_cfg.tcpPort; }
    void setPort(int v);
    int udpPort() const { return m_cfg.udpPort; }
    void setUdpPort(int v);
    QString password() const { return m_cfg.password; }
    void setPassword(const QString &v);
    bool encrypt() const { return m_cfg.encrypt; }
    void setEncrypt(bool v);
    bool autoReconnect() const { return m_cfg.autoReconnect; }
    void setAutoReconnect(bool v);
    QString codec() const { return m_cfg.codec; }
    void setCodec(const QString &v);
    int jitterTarget() const { return m_cfg.jitterMs; }
    void setJitterTarget(int v);
    int speechPreset() const { return m_speechPreset; }
    void setSpeechPreset(int v);
    double rxGain() const { return m_cfg.rxGain; }
    void setRxGain(double v);
    double txGain() const { return m_cfg.txGain; }
    void setTxGain(double v);
    QVariantList inputDevices() const { return m_inputs; }
    QVariantList outputDevices() const { return m_outputs; }
    int inputDevice() const { return m_cfg.inputDevice; }
    void setInputDevice(int v);
    int outputDevice() const { return m_cfg.outputDevice; }
    void setOutputDevice(int v);
    bool pttOnVolumeKey() const { return m_pttOnVolumeKey; }
    void setPttOnVolumeKey(bool v);
    bool rigctldEnabled() const { return m_rigctldEnabled; }
    void setRigctldEnabled(bool v);
    int rigctldPort() const { return 4532; }
    bool isAndroid() const;
    int statusBarHeight() const;
    QString version() const;

    int rttMs() const { return m_rtt; }
    int lostFrames() const { return m_lost; }
    int jitterMs() const { return m_jitter; }
    double rxLevel() const { return m_rxLevel; }
    double txLevel() const { return m_txLevel; }
    double gainReductionDb() const { return m_reduction; }
    QString logText() const { return m_log.join(QLatin1Char('\n')); }

    // VolumePttSink
    bool volumePttWanted() const override { return m_pttOnVolumeKey; }
    void volumePttChanged(bool pressed) override;

signals:
    void connectionChanged();
    void retryChanged();
    void stateChanged();
    void frequencyChanged();
    void valuesChanged();
    void widthChanged();
    void readProgressChanged();
    void stepChanged();
    void cwChanged();
    void settingsChanged();
    void devicesChanged();
    void statsChanged();
    void logChanged();
    void pttLocalChanged();
    // A message for the operator: shown briefly over the panel.
    void noticeRaised(const QString &text);

private:
    void onState(const Ft891State &st);
    void onValues(const QVariantMap &values, bool full);
    void appendLog(const QString &line);
    void sendSet(const QString &code, const QStringList &values, bool confirmed);
    void localStore(const QString &code, const QString &raw);
    void flushTune();
    void pushSpeech();
    void loadSettings();
    void saveSettings() const;
    quint64 displayedFreq() const;
    static QString formatFreq(quint64 hz);
    QStringList currentParams(const CatCommand &c) const;

    QThread     m_netThread;
    ClientCore *m_core = nullptr;
    RigctldServer *m_rigctld = nullptr;
    ClientConfig m_cfg;

    bool    m_connected = false;
    QString m_status;
    int     m_retrySeconds = 0;
    int     m_retryAttempt = 0;
    bool    m_rxOnly = false;
    QString m_serverVersion;
    int     m_region = 1;

    Ft891State m_state;
    QHash<QString, QString> m_values;
    int     m_revision = 0;
    int     m_readDone = 0;
    int     m_readTotal = 0;
    bool    m_pttLocal = false;

    // Knob: the frequency shown while the operator tunes, ahead of the radio.
    quint64       m_localFreq = 0;
    QElapsedTimer m_localClock;
    bool          m_tunePending = false;
    QTimer       *m_tuneTimer = nullptr;
    int           m_stepIndex = 1;

    int         m_wpm = 20;
    QString     m_myCall;
    QStringList m_macros;

    int     m_speechPreset = 1;
    bool    m_pttOnVolumeKey = false;
    bool    m_rigctldEnabled = false;
    QVariantList m_inputs, m_outputs;

    int     m_rtt = 0, m_lost = 0, m_jitter = 0;
    double  m_rxLevel = 0, m_txLevel = 0, m_reduction = 0;
    QStringList m_log;
};

} // namespace rr
