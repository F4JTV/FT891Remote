#include "ft891bridge.h"

#include <QSet>

#include "../../common/audioengine.h"
#include "../../common/catcommands.h"
#include "../../common/ft891.h"
#include "../rigctldserver.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QLocale>
#include <QSettings>
#include <QTimer>

#ifndef RR_VERSION
#define RR_VERSION "0.0.0"
#endif

namespace rr {

namespace {

constexpr int kKnobHoldMs = 500;       // display follows the knob this long
constexpr int kTuneFlushMs = 40;       // at most 25 frequency frames a second
constexpr quint64 kMinHz = 30000;
constexpr quint64 kMaxHz = 56000000;

const int kSteps[] = {1, 10, 100, 500, 1000, 5000, 10000, 100000};
constexpr int kStepCount = int(sizeof(kSteps) / sizeof(kSteps[0]));

QString typeName(CatParam::Type t)
{
    switch (t) {
    case CatParam::Enum: return QStringLiteral("enum");
    case CatParam::Text: return QStringLiteral("text");
    default:             return QStringLiteral("range");
    }
}

QVariantList deviceList(const QList<AudioDevice> &devices)
{
    QVariantList out;
    for (const AudioDevice &d : devices)
        out << QVariantMap{{QStringLiteral("id"), d.index}, {QStringLiteral("name"), d.name}};
    return out;
}

} // namespace

// ================================================================== set-up
Ft891Bridge::Ft891Bridge(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<rr::Ft891State>("rr::Ft891State");
    qRegisterMetaType<rr::ClientConfig>("rr::ClientConfig");
    qRegisterMetaType<rr::SpeechSettings>("rr::SpeechSettings");

    m_macros = {QStringLiteral("CQ CQ DE {MYCALL} {MYCALL} K"),
                QStringLiteral("{MYCALL}"),
                QStringLiteral("TU 5NN"),
                QStringLiteral("RR TU 73"),
                QStringLiteral("QRZ?"),
                QStringLiteral("AGN?"),
                QStringLiteral("73 DE {MYCALL} SK"),
                QStringLiteral("QRL?")};

    m_core = new ClientCore;
    m_core->moveToThread(&m_netThread);
    m_netThread.start(QThread::TimeCriticalPriority);

    connect(m_core, &ClientCore::connectionChanged, this, [this](bool up, const QString &msg) {
        m_connected = up;
        m_status = msg;
        if (up) startAndroidService(); else stopAndroidService();
        if (!up) {
            m_state = Ft891State();
            m_values.clear();
            ++m_revision;
            m_readDone = m_readTotal = 0;
            m_pttLocal = false;
            emit pttLocalChanged();
            emit stateChanged();
            emit frequencyChanged();
            emit valuesChanged();
            emit widthChanged();
            emit readProgressChanged();
        }
        appendLog(msg);
        emit connectionChanged();
    });
    connect(m_core, &ClientCore::receiveOnly, this, [this](bool on) {
        m_rxOnly = on;
        emit connectionChanged();
    });
    connect(m_core, &ClientCore::serverInfo, this, [this](int region, const QString &ver) {
        m_region = region;
        m_serverVersion = ver;
        emit connectionChanged();
    });
    connect(m_core, &ClientCore::stateChanged, this, &Ft891Bridge::onState);
    connect(m_core, &ClientCore::valuesChanged, this, &Ft891Bridge::onValues);
    connect(m_core, &ClientCore::readProgress, this, [this](int done, int total) {
        m_readDone = done;
        m_readTotal = total;
        emit readProgressChanged();
    });
    connect(m_core, &ClientCore::logMessage, this, &Ft891Bridge::appendLog);
    connect(m_core, &ClientCore::notice, this, &Ft891Bridge::noticeRaised);
    connect(m_core, &ClientCore::retryCountdown, this, [this](int seconds, int attempt) {
        m_retrySeconds = seconds;
        m_retryAttempt = attempt;
        emit retryChanged();
    });
    connect(m_core, &ClientCore::statsUpdated, this,
            [this](int rtt, int lost, int jitter, float rx, float tx, float reduction, bool, bool) {
                m_rtt = rtt; m_lost = lost; m_jitter = jitter;
                m_rxLevel = rx; m_txLevel = tx; m_reduction = reduction;
                emit statsChanged();
            });

    m_tuneTimer = new QTimer(this);
    m_tuneTimer->setSingleShot(true);
    m_tuneTimer->setInterval(kTuneFlushMs);
    connect(m_tuneTimer, &QTimer::timeout, this, &Ft891Bridge::flushTune);

    loadSettings();
    refreshDevices();
    pushSpeech();
    setVolumePttSink(this);

    if (m_rigctldEnabled) {
        m_rigctldEnabled = false;
        setRigctldEnabled(true);
    }
}

Ft891Bridge::~Ft891Bridge()
{
    setVolumePttSink(nullptr);
    saveSettings();
    QMetaObject::invokeMethod(m_core, "disconnectFromStation", Qt::BlockingQueuedConnection);
    m_netThread.quit();
    m_netThread.wait(2000);
    delete m_core;
}

QString Ft891Bridge::version() const { return QStringLiteral(RR_VERSION); }
int Ft891Bridge::statusBarHeight() const { return androidStatusBarHeight(); }

bool Ft891Bridge::isAndroid() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

// ============================================================== connection
void Ft891Bridge::connectToStation()
{
    saveSettings();
    appendLog(tr("Connecting to %1:%2…").arg(m_cfg.host).arg(m_cfg.tcpPort));
    QMetaObject::invokeMethod(m_core, "connectToStation", Qt::QueuedConnection,
                              Q_ARG(rr::ClientConfig, m_cfg));
}

void Ft891Bridge::disconnectFromStation()
{
    QMetaObject::invokeMethod(m_core, "disconnectFromStation", Qt::QueuedConnection);
}

QString Ft891Bridge::retryText() const
{
    if (m_retrySeconds <= 0) return QString();
    return tr("Retrying in %1 s (attempt %2)").arg(m_retrySeconds).arg(m_retryAttempt);
}

void Ft891Bridge::refreshDevices()
{
    AudioEngine::initialiseLibrary();
    m_inputs = deviceList(AudioEngine::inputDevices());
    m_outputs = deviceList(AudioEngine::outputDevices());
    // A saved index that no longer exists falls back to the system default.
    auto known = [](const QVariantList &l, int id) {
        for (const QVariant &v : l)
            if (v.toMap().value(QStringLiteral("id")).toInt() == id) return true;
        return false;
    };
    if (m_cfg.outputDevice < 0 || !known(m_outputs, m_cfg.outputDevice))
        m_cfg.outputDevice = AudioEngine::defaultOutput();
    if (m_cfg.inputDevice >= 0 && !known(m_inputs, m_cfg.inputDevice))
        m_cfg.inputDevice = AudioEngine::defaultInput();
    emit devicesChanged();
    emit settingsChanged();
}

// =================================================================== state
void Ft891Bridge::onState(const Ft891State &st)
{
    const bool freqMoved = st.freqA != m_state.freqA;
    const bool modeChanged = st.mode != m_state.mode;
    m_state = st;
    if (m_rigctld) m_rigctld->updateState(st);
    if (m_pttLocal && !st.ptt && !st.radioOn) {
        m_pttLocal = false;
        emit pttLocalChanged();
    }
    emit stateChanged();
    if (freqMoved && !knobActive()) emit frequencyChanged();
    // The same WIDTH step is another bandwidth in another mode.
    if (modeChanged) emit widthChanged();
}

void Ft891Bridge::onValues(const QVariantMap &values, bool full)
{
    if (full) m_values.clear();
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_values.insert(it.key(), it.value().toString());
    if (m_rigctld) m_rigctld->updateValues(values);
    if (values.contains(QStringLiteral("SPEED"))) {
        const int w = values.value(QStringLiteral("SPEED")).toInt();
        if (w >= 4 && w != m_wpm) {
            m_wpm = w;
            emit cwChanged();
        }
    }
    ++m_revision;
    emit valuesChanged();
    emit widthChanged();
}

QString Ft891Bridge::radioStatus() const
{
    if (!m_connected) return tr("Not connected");
    if (!m_state.linkOpen)
        return m_state.error.isEmpty() ? tr("CAT port closed on the station") : m_state.error;
    if (!m_state.radioOn) return tr("The radio does not answer — switched off?");
    return QString();
}

QString Ft891Bridge::formatFreq(quint64 hz)
{
    if (hz == 0) return QStringLiteral("--.---.---");
    // 14.074.000 — the way the radio's display groups digits.
    const quint64 mhz = hz / 1000000;
    const quint64 khz = (hz / 1000) % 1000;
    const quint64 h = hz % 1000;
    return QStringLiteral("%1.%2.%3").arg(mhz)
        .arg(khz, 3, 10, QLatin1Char('0')).arg(h, 3, 10, QLatin1Char('0'));
}

QStringList Ft891Bridge::modes() const { return ft891::modeFamilies(); }
QString Ft891Bridge::modeFamily() const { return ft891::modeFamily(m_state.mode); }

bool Ft891Bridge::split() const
{
    const QString v = m_values.value(QStringLiteral("SPL"));
    return !v.isEmpty() && !v.startsWith(QLatin1Char('0'));
}

int Ft891Bridge::bandIndex() const { return ft891::bandIndexFor(displayedFreq()); }

QStringList Ft891Bridge::bands() const
{
    QStringList out;
    for (const ft891::BandInfo &b : ft891::bands())
        // "20 m", but "GEN" and "MW" as they are.
        out << (b.name.at(0).isDigit() ? b.name + QStringLiteral(" m") : b.name);
    return out;
}

// ================================================================== meters
QString Ft891Bridge::sMeterText() const { return ft891::sMeterText(m_state.sMeter); }

double Ft891Bridge::poMeter() const
{
    return qBound(0.0, ft891::powerWatts(m_state.po) / 100.0, 1.0);
}

QString Ft891Bridge::poText() const
{
    return QStringLiteral("%1 W").arg(ft891::powerWatts(m_state.po), 0, 'f', 0);
}

double Ft891Bridge::swrMeter() const
{
    // Full scale at 3:1, where most operators stop.
    const double s = ft891::swrValue(m_state.swr);
    return qBound(0.0, (s - 1.0) / 2.0, 1.0);
}

QString Ft891Bridge::swrText() const
{
    const double s = ft891::swrValue(m_state.swr);
    return s >= 9.9 ? QStringLiteral("∞") : QStringLiteral("%1").arg(s, 0, 'f', 1);
}

QString Ft891Bridge::compText() const
{
    return QStringLiteral("%1 dB").arg(ft891::compDb(m_state.comp), 0, 'f', 0);
}

// ================================================================== tuning
QVariantList Ft891Bridge::tuneSteps() const
{
    QVariantList out;
    for (int s : kSteps) out << s;
    return out;
}

void Ft891Bridge::setStepIndex(int i)
{
    i = qBound(0, i, kStepCount - 1);
    if (i == m_stepIndex) return;
    m_stepIndex = i;
    emit stepChanged();
    saveSettings();
}

int Ft891Bridge::stepHz() const { return kSteps[qBound(0, m_stepIndex, kStepCount - 1)]; }
void Ft891Bridge::stepUp()   { setStepIndex(m_stepIndex + 1); }
void Ft891Bridge::stepDown() { setStepIndex(m_stepIndex - 1); }

bool Ft891Bridge::knobActive() const
{
    return m_localFreq != 0 && m_localClock.isValid() && m_localClock.elapsed() < kKnobHoldMs;
}

quint64 Ft891Bridge::displayedFreq() const
{
    return knobActive() ? m_localFreq : m_state.freqA;
}

void Ft891Bridge::tune(int detents)
{
    if (detents == 0) return;
    const qint64 step = stepHz();
    const qint64 base = qint64(displayedFreq());
    if (base == 0) return;
    // Onto the step grid, then the detents: from 14.074.030 with a 1 kHz
    // step, one detent up gives 14.075.000, as on the radio.
    qint64 target = base + detents * step;
    target = (target + (detents > 0 ? 0 : step - 1)) / step * step;
    if (detents > 0 && target <= base) target += step;
    tuneHz(int(target - base));
}

void Ft891Bridge::tuneHz(int hz)
{
    if (hz == 0 || !m_connected) return;
    const qint64 base = qint64(displayedFreq());
    if (base == 0) return;
    const quint64 target = quint64(qBound<qint64>(kMinHz, base + hz, kMaxHz));
    m_localFreq = target;
    m_localClock.restart();
    m_tunePending = true;
    if (!m_tuneTimer->isActive()) m_tuneTimer->start();
    emit frequencyChanged();
    // Once the knob is still, the radio's own reading is shown again.
    QTimer::singleShot(kKnobHoldMs + 20, this, [this] {
        if (!knobActive()) emit frequencyChanged();
    });
}

void Ft891Bridge::flushTune()
{
    if (!m_tunePending) return;
    m_tunePending = false;
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("freq")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("hz"), double(m_localFreq)},
                                                              {QStringLiteral("vfo"), QStringLiteral("A")}})));
}

void Ft891Bridge::setFrequency(double hz)
{
    if (hz < kMinHz || hz > kMaxHz) {
        emit noticeRaised(tr("The FT-891 covers 30 kHz to 56 MHz"));
        return;
    }
    m_localFreq = quint64(hz + 0.5);
    m_localClock.restart();
    m_tunePending = true;
    flushTune();
    emit frequencyChanged();
    QTimer::singleShot(kKnobHoldMs + 20, this, [this] {
        if (!knobActive()) emit frequencyChanged();
    });
}

void Ft891Bridge::setFrequencyB(double hz)
{
    if (hz < kMinHz || hz > kMaxHz) {
        emit noticeRaised(tr("The FT-891 covers 30 kHz to 56 MHz"));
        return;
    }
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("freq")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("hz"), hz},
                                                              {QStringLiteral("vfo"), QStringLiteral("B")}})));
}

bool Ft891Bridge::enterFrequency(const QString &text, bool vfoB)
{
    bool ok = false;
    const quint64 hz = parseFrequency(text, &ok);
    if (!ok || hz < kMinHz || hz > kMaxHz) return false;
    if (vfoB) setFrequencyB(double(hz)); else setFrequency(double(hz));
    return true;
}

void Ft891Bridge::setMode(const QString &name)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("mode")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("v"), name}})));
}

void Ft891Bridge::selectBand(int index)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("band")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("index"), index}})));
}

void Ft891Bridge::bandUp()   { run(QStringLiteral("BU")); }
void Ft891Bridge::bandDown() { run(QStringLiteral("BD")); }

void Ft891Bridge::clarifier(int hz)
{
    if (hz == 0) return;
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("clar")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("delta"), hz}})));
}

void Ft891Bridge::selectMemory(int channel)
{
    sendSet(QStringLiteral("MC"), {QString::number(qBound(1, channel, 99))}, false);
}

void Ft891Bridge::power(bool on)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("power")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("on"), on}})));
}

// ================================================================ settings
QStringList Ft891Bridge::withoutCompanions(const QStringList &codes) const
{
    QSet<QString> companions;
    for (const QString &code : codes)
        if (const CatCommand *c = ft891::command(code))
            if (!c->companion.isEmpty()) companions.insert(c->companion);
    QStringList out;
    for (const QString &code : codes)
        if (!companions.contains(code)) out << code;
    return out;
}

QVariantMap Ft891Bridge::describe(const QString &code) const
{
    const CatCommand *c = ft891::command(code);
    if (!c) return {};
    QVariantList params;
    for (const CatParam &p : c->params) {
        // Only the values that can be sent: the others are answers only.
        QVariantList values;
        for (const CatParamValue &v : p.values)
            if (!v.readOnly)
                values << QVariantMap{{QStringLiteral("v"), v.value}, {QStringLiteral("label"), v.label}};
        params << QVariantMap{
            {QStringLiteral("name"), p.name},
            {QStringLiteral("type"), typeName(p.type)},
            {QStringLiteral("min"), p.min},
            {QStringLiteral("max"), p.max},
            {QStringLiteral("step"), qMax(1, p.step)},
            {QStringLiteral("signed"), p.isSigned},
            {QStringLiteral("unit"), p.dispUnit.isEmpty() ? p.unit : p.dispUnit},
            {QStringLiteral("maxLength"), p.maxLength},
            {QStringLiteral("values"), values},
        };
    }
    const CatGroup *g = ft891::groupOf(code);
    return QVariantMap{
        {QStringLiteral("code"), c->code},
        {QStringLiteral("name"), c->name},
        {QStringLiteral("note"), c->note},
        {QStringLiteral("companion"), c->companion},
        {QStringLiteral("tried"), c->tried},
        {QStringLiteral("verified"), c->verified},
        {QStringLiteral("confirm"), c->confirm},
        {QStringLiteral("canSet"), c->canSet()},
        {QStringLiteral("canRead"), c->canRead()},
        {QStringLiteral("group"), g ? g->name : QString()},
        {QStringLiteral("params"), params},
    };
}

QStringList Ft891Bridge::groupCodes(const QString &group) const
{
    QStringList out;
    for (const CatGroup &g : ft891::description().groups)
        if (g.name == group)
            for (const CatCommand &c : g.commands) out << c.code;
    return out;
}

QStringList Ft891Bridge::menuGroups() const
{
    QStringList out;
    for (const CatGroup &g : ft891::description().groups)
        if (g.name.startsWith(QLatin1String("MENU"))) out << g.name;
    return out;
}

bool Ft891Bridge::groupComplete(const QString &group) const
{
    for (const CatGroup &g : ft891::description().groups) {
        if (g.name != group) continue;
        for (const CatCommand &c : g.commands)
            if (c.canRead() && (!c.params.isEmpty() || !c.canSet()) && !m_values.contains(c.code))
                return false;
    }
    return true;
}

bool Ft891Bridge::hasValue(const QString &code) const { return m_values.contains(code); }
QString Ft891Bridge::value(const QString &code) const { return m_values.value(code); }

QStringList Ft891Bridge::currentParams(const CatCommand &c) const
{
    QStringList vals = c.splitAnswer(m_values.value(c.code));
    for (int i = 0; i < c.params.size(); ++i)
        if (i >= vals.size() || vals.at(i).isEmpty()) {
            if (i >= vals.size()) vals << c.params.at(i).defaultValue();
            else vals[i] = c.params.at(i).defaultValue();
        }
    return vals;
}

QString Ft891Bridge::paramValue(const QString &code, int index) const
{
    const CatCommand *c = ft891::command(code);
    if (!c || !m_values.contains(code)) return QString();
    QString v = c->splitAnswer(m_values.value(code)).value(index);
    // A keyer memory ends with "}", the radio's end-of-text mark, and an
    // empty one is answered "}" alone: not text to show or to edit.
    if (index < c->params.size() && c->params.at(index).type == CatParam::Text && v.endsWith(QLatin1Char('}')))
        v.chop(1);
    return v;
}

int Ft891Bridge::paramInt(const QString &code, int index) const
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return 0;
    bool ok = false;
    const int v = paramValue(code, index).toInt(&ok);
    return ok ? v : c->params.at(index).min;
}

int Ft891Bridge::enumIndex(const QString &code, int index) const
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return -1;
    QString v = paramValue(code, index);
    const QList<CatParamValue> &values = c->params.at(index).values;
    // An answer-only value selects the settable one it stands for.
    for (const CatParamValue &pv : values)
        if (pv.readOnly && pv.value == v) { v = pv.as; break; }
    int i = 0;
    for (const CatParamValue &pv : values) {
        if (pv.readOnly) continue;
        if (pv.value == v) return i;
        ++i;
    }
    return -1;
}

bool Ft891Bridge::isOn(const QString &code) const
{
    // « 0 », « 00 » and « 000 » are all OFF: switches differ in width.
    const QString v = paramValue(code, 0);
    for (const QChar ch : v)
        if (ch != QLatin1Char('0')) return true;
    return false;
}

QString Ft891Bridge::display(const QString &code) const
{
    const CatCommand *c = ft891::command(code);
    if (!c) return QString();
    if (!m_values.contains(code)) return QStringLiteral("—");
    if (code == QLatin1String("WDH")) return widthText();
    return ft891::displayValue(*c, m_values.value(code));
}

QString Ft891Bridge::describeStep(const QString &code, int index, int step) const
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return QString::number(step);
    if (code == QLatin1String("WDH") && index == 1) {
        const int hz = ft891::widthHz(m_state.mode, narrowOn(), step);
        if (hz <= 0) return QStringLiteral("—");
        // The slider shows step 00 at the step with the same bandwidth.
        const bool isDefault = paramInt(code, 1) == 0
                               && step == ft891::widthDefaultStep(m_state.mode, narrowOn());
        return isDefault ? tr("%1 Hz (default)").arg(hz) : tr("%1 Hz").arg(hz);
    }
    return c->params.at(index).describe(step);
}

bool Ft891Bridge::narrowOn() const
{
    return m_values.value(QStringLiteral("NAR")) == QLatin1String("1");
}

bool Ft891Bridge::widthAvailable() const
{
    return ft891::widthRange(m_state.mode, narrowOn()).available;
}

QString Ft891Bridge::widthText() const
{
    if (!m_values.contains(QStringLiteral("WDH")) || !widthAvailable()) return QStringLiteral("—");
    // WIDTH off: the mode's default width, whatever the step says.
    const int step = isOn(QStringLiteral("WDH")) ? paramInt(QStringLiteral("WDH"), 1) : 0;
    const int hz = ft891::widthHz(m_state.mode, narrowOn(), step);
    // A step the radio reports but the table does not have for this mode:
    // shown as it is rather than as a made-up bandwidth.
    return hz > 0 ? tr("%1 Hz").arg(hz) : tr("step %1").arg(step, 2, 10, QLatin1Char('0'));
}

QVariantMap Ft891Bridge::paramRange(const QString &code, int index) const
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return {};
    const CatParam &p = c->params.at(index);
    QVariantMap r{{QStringLiteral("min"), p.min}, {QStringLiteral("max"), p.max},
                  {QStringLiteral("step"), qMax(1, p.step)}, {QStringLiteral("available"), true}};
    if (code == QLatin1String("WDH") && index == 1) {
        const ft891::WidthRange w = ft891::widthRange(m_state.mode, narrowOn());
        r.insert(QStringLiteral("available"), w.available);
        if (w.available) {
            r.insert(QStringLiteral("min"), w.first);
            r.insert(QStringLiteral("max"), w.last);
        } else {
            r.insert(QStringLiteral("reason"),
                     tr("No width setting in %1").arg(m_state.mode.isEmpty() ? tr("this mode") : m_state.mode));
        }
    }
    return r;
}

QString Ft891Bridge::describeCurrent(const QString &code, int index) const
{
    if (!m_values.contains(code)) return QStringLiteral("—");
    if (code == QLatin1String("WDH") && index == 1) {
        const int step = paramInt(code, index);
        if (!ft891::widthStepValid(m_state.mode, narrowOn(), step))
            return tr("step %1").arg(step, 2, 10, QLatin1Char('0'));
        return describeStep(code, index, paramPosition(code, index));
    }
    return describeStep(code, index, paramInt(code, index));
}

int Ft891Bridge::paramPosition(const QString &code, int index) const
{
    const int v = paramInt(code, index);
    if (code == QLatin1String("WDH") && index == 1 && v == 0)
        return ft891::widthDefaultStep(m_state.mode, narrowOn());
    return v;
}

void Ft891Bridge::localStore(const QString &code, const QString &raw)
{
    // Shown at once; the server's read-back confirms or corrects it within
    // a poll cycle.
    if (m_values.value(code) == raw) return;
    m_values.insert(code, raw);
    ++m_revision;
    emit valuesChanged();
    if (code == QLatin1String("WDH")) emit widthChanged();
}

void Ft891Bridge::sendSet(const QString &code, const QStringList &values, bool confirmed)
{
    const CatCommand *c = ft891::command(code);
    if (!c || !c->canSet()) return;
    if (!m_connected) {
        emit noticeRaised(tr("Not connected"));
        return;
    }
    QVariantMap args{{QStringLiteral("code"), code}, {QStringLiteral("v"), values}};
    if (confirmed) args.insert(QStringLiteral("confirmed"), true);
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("set")), Q_ARG(QVariantMap, args));

    if (!c->params.isEmpty() && c->canRead() && (c->confirm.isEmpty() || confirmed)) {
        // The answer the radio would give: the frame, without its prefix.
        QString frame = c->buildSet(values);
        while (frame.endsWith(QLatin1Char(';'))) frame.chop(1);
        const QString prefix = c->answerPrefix();
        if (frame.startsWith(prefix)) localStore(code, frame.mid(prefix.size()));
    }
}

void Ft891Bridge::setParam(const QString &code, int index, const QString &value, bool confirmed)
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return;
    QStringList vals = currentParams(*c);
    vals[index] = value;
    sendSet(code, vals, confirmed);
}

void Ft891Bridge::setEnumIndex(const QString &code, int index, int valueIndex, bool confirmed)
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return;
    QList<CatParamValue> values;
    for (const CatParamValue &pv : c->params.at(index).values)
        if (!pv.readOnly) values << pv;
    if (valueIndex < 0 || valueIndex >= values.size()) return;
    setParam(code, index, values.at(valueIndex).value, confirmed);
}

// A switch is set with its own values: MON is ML0000/ML0001, CONTOUR
// CO000000/CO000001. A bare "0" or "1", which the switches used to send, is
// not in those lists: the server refused it and the switch did nothing.
void Ft891Bridge::setSwitch(const QString &code, int index, bool on, bool confirmed)
{
    const CatCommand *c = ft891::command(code);
    if (!c || index < 0 || index >= c->params.size()) return;
    const CatParam &p = c->params.at(index);
    QString value = on ? QStringLiteral("1") : QStringLiteral("0");
    if (p.type == CatParam::Enum && p.values.size() >= 2) {
        // Off is the value made of zeros; on, the first other settable one.
        auto isZero = [](const QString &v) {
            for (const QChar ch : v) if (ch != QLatin1Char('0')) return false;
            return true;
        };
        for (const CatParamValue &pv : p.values) {
            if (pv.readOnly) continue;
            if (on != isZero(pv.value)) { value = pv.value; break; }
        }
    }
    setParam(code, index, value, confirmed);
}

bool Ft891Bridge::isOnAt(const QString &code, int index) const
{
    const QString v = paramValue(code, index);
    for (const QChar ch : v)
        if (ch != QLatin1Char('0')) return true;
    return false;
}

void Ft891Bridge::toggle(const QString &code)
{
    setSwitch(code, 0, !isOn(code));
}

void Ft891Bridge::run(const QString &code, bool confirmed)
{
    sendSet(code, {}, confirmed);
}

void Ft891Bridge::readCodes(const QStringList &codes)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("read")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("codes"), codes}})));
}

void Ft891Bridge::readGroup(const QString &group)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("read")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("group"), group}})));
}

void Ft891Bridge::readAll()
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("read")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("all"), true}})));
}

// ============================================================= PTT and CW
void Ft891Bridge::setPtt(bool on)
{
    if (on && !m_connected) return;
    if (on && m_rxOnly) {
        emit noticeRaised(tr("Receive only: no microphone on this device"));
        return;
    }
    if (on && !m_state.txAllowed) {
        emit noticeRaised(tr("Out of band: transmission refused"));
        return;
    }
    if (m_pttLocal == on) return;
    m_pttLocal = on;
    emit pttLocalChanged();
    QMetaObject::invokeMethod(m_core, "setPtt", Qt::QueuedConnection, Q_ARG(bool, on));
}

void Ft891Bridge::volumePttChanged(bool pressed)
{
    // Called from Android's UI thread.
    QMetaObject::invokeMethod(this, [this, pressed] { setPtt(pressed); }, Qt::QueuedConnection);
}

void Ft891Bridge::startTune()
{
    QMetaObject::invokeMethod(m_core, "startTune", Qt::QueuedConnection);
}

QString Ft891Bridge::expandMacro(const QString &text) const
{
    QString t = text;
    t.replace(QStringLiteral("{MYCALL}"), m_myCall.isEmpty() ? QStringLiteral("N0CALL") : m_myCall,
              Qt::CaseInsensitive);
    return t;
}

void Ft891Bridge::sendCw(const QString &text)
{
    const QString t = ft891::keyerText(expandMacro(text));
    if (t.isEmpty()) return;
    QMetaObject::invokeMethod(m_core, "sendMorse", Qt::QueuedConnection, Q_ARG(QString, t));
}

void Ft891Bridge::stopCw()
{
    QMetaObject::invokeMethod(m_core, "stopMorse", Qt::QueuedConnection);
}

void Ft891Bridge::playCwMemory(int n)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("cwmem")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("n"), n}})));
}

// ================================================================ memories
QStringList Ft891Bridge::memoryChannels() const { return ft891::memoryChannels(); }
QStringList Ft891Bridge::exactModes() const { return ft891::modeNames(); }

int Ft891Bridge::memoriesRead() const
{
    int n = 0;
    for (const QString &ch : ft891::memoryChannels())
        if (m_values.contains(ft891::memoryKey(ch))) ++n;
    return n;
}

int Ft891Bridge::memoriesUsed() const
{
    int n = 0;
    for (const QString &ch : ft891::memoryChannels())
        if (!m_values.value(ft891::memoryKey(ch)).isEmpty()) ++n;
    return n;
}

QVariantMap Ft891Bridge::memory(const QString &ch) const
{
    const QString key = ft891::memoryKey(ch);
    QVariantMap out{{QStringLiteral("ch"), ch},
                    {QStringLiteral("known"), m_values.contains(key)},
                    {QStringLiteral("empty"), true}};
    const QString raw = m_values.value(key);
    if (raw.isEmpty()) return out;
    const ft891::IfInfo m = ft891::parseIf(QStringLiteral("IF") + raw);
    if (!m.ok) return out;
    const ft891::ModeInfo *mi = ft891::modeByCode(m.mode);
    out.insert(QStringLiteral("empty"), false);
    out.insert(QStringLiteral("hz"), double(m.freq));
    out.insert(QStringLiteral("freqText"), formatFreq(m.freq));
    out.insert(QStringLiteral("mode"), mi ? mi->name : QString());
    out.insert(QStringLiteral("clarOn"), m.clarOn);
    out.insert(QStringLiteral("clarOffset"), m.clarOffset);
    out.insert(QStringLiteral("tone"), m.tone);
    out.insert(QStringLiteral("shift"), m.shift);
    return out;
}

QVariantMap Ft891Bridge::vfoAsMemory() const
{
    return {{QStringLiteral("hz"), double(m_state.freqA)},
            {QStringLiteral("freqText"), formatFreq(m_state.freqA)},
            {QStringLiteral("mode"), m_state.mode},
            {QStringLiteral("clarOn"), m_state.rxClar || m_state.txClar},
            {QStringLiteral("clarOffset"), m_state.clarOffset},
            {QStringLiteral("tone"), m_state.toneMode},
            {QStringLiteral("shift"), m_state.rptShift}};
}

void Ft891Bridge::readMemories()
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("memread")), Q_ARG(QVariantMap, QVariantMap()));
}

bool Ft891Bridge::writeMemory(const QVariantMap &m)
{
    const double hz = m.value(QStringLiteral("hz")).toDouble();
    if (!m_connected || !ft891::isMemoryChannel(m.value(QStringLiteral("ch")).toString())
        || hz < 30000 || hz > 56000000 || !ft891::modeByName(m.value(QStringLiteral("mode")).toString()))
        return false;
    QVariantMap args{{QStringLiteral("ch"), m.value(QStringLiteral("ch"))},
                     {QStringLiteral("hz"), qint64(hz)},
                     {QStringLiteral("mode"), m.value(QStringLiteral("mode"))},
                     {QStringLiteral("clarOn"), m.value(QStringLiteral("clarOn")).toBool()},
                     {QStringLiteral("clar"), m.value(QStringLiteral("clarOffset")).toInt()},
                     {QStringLiteral("tone"), m.value(QStringLiteral("tone")).toInt()},
                     {QStringLiteral("shift"), m.value(QStringLiteral("shift")).toInt()}};
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("memwrite")), Q_ARG(QVariantMap, args));
    return true;
}

void Ft891Bridge::recallMemory(const QString &ch)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("memrecall")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("ch"), ch}})));
}

double Ft891Bridge::frequencyFromText(const QString &text) const
{
    bool ok = false;
    const quint64 hz = parseFrequency(text, &ok);
    return ok && hz >= 30000 && hz <= 56000000 ? double(hz) : 0.0;
}

void Ft891Bridge::playCwMessage(int n)
{
    QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("cwmem")),
                              Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("n"), n},
                                                              {QStringLiteral("message"), true}})));
}

void Ft891Bridge::setWpm(int v)
{
    v = qBound(4, v, 60);
    if (v == m_wpm) return;
    m_wpm = v;
    QMetaObject::invokeMethod(m_core, "setKeySpeed", Qt::QueuedConnection, Q_ARG(int, v));
    localStore(QStringLiteral("SPEED"), QStringLiteral("%1").arg(v, 3, 10, QLatin1Char('0')));
    emit cwChanged();
}

void Ft891Bridge::setMyCall(const QString &v)
{
    const QString c = v.trimmed().toUpper();
    if (c == m_myCall) return;
    m_myCall = c;
    saveSettings();
    emit cwChanged();
}

void Ft891Bridge::setCwMacro(int index, const QString &text)
{
    if (index < 0 || index >= m_macros.size()) return;
    m_macros[index] = text.toUpper();
    saveSettings();
    emit cwChanged();
}

void Ft891Bridge::appendLog(const QString &line)
{
    if (line.isEmpty()) return;
    m_log << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + line;
    while (m_log.size() > 300) m_log.removeFirst();
    emit logChanged();
}

void Ft891Bridge::clearLog() { m_log.clear(); emit logChanged(); }

// ============================================================ preferences
void Ft891Bridge::setHost(const QString &v)     { if (v == m_cfg.host) return; m_cfg.host = v.trimmed(); emit settingsChanged(); }
void Ft891Bridge::setPort(int v)                { if (v == m_cfg.tcpPort) return; m_cfg.tcpPort = quint16(v); emit settingsChanged(); }
void Ft891Bridge::setUdpPort(int v)             { if (v == m_cfg.udpPort) return; m_cfg.udpPort = quint16(v); emit settingsChanged(); }
void Ft891Bridge::setPassword(const QString &v) { if (v == m_cfg.password) return; m_cfg.password = v; emit settingsChanged(); }
void Ft891Bridge::setEncrypt(bool v)            { if (v == m_cfg.encrypt) return; m_cfg.encrypt = v; emit settingsChanged(); }

void Ft891Bridge::setAutoReconnect(bool v)
{
    if (v == m_cfg.autoReconnect) return;
    m_cfg.autoReconnect = v;
    QMetaObject::invokeMethod(m_core, "setAutoReconnect", Qt::QueuedConnection, Q_ARG(bool, v));
    emit settingsChanged();
}

void Ft891Bridge::setCodec(const QString &v)
{
    if (v == m_cfg.codec) return;
    m_cfg.codec = v;
    if (m_connected)
        QMetaObject::invokeMethod(m_core, "setCodec", Qt::QueuedConnection,
                                  Q_ARG(QString, v), Q_ARG(int, m_cfg.bitrate));
    emit settingsChanged();
}

void Ft891Bridge::setJitterTarget(int v)
{
    v = qBound(10, v, 400);
    if (v == m_cfg.jitterMs) return;
    m_cfg.jitterMs = v;
    QMetaObject::invokeMethod(m_core, "setJitterMs", Qt::QueuedConnection, Q_ARG(int, v));
    emit settingsChanged();
}

void Ft891Bridge::setSpeechPreset(int v)
{
    if (v == m_speechPreset) return;
    m_speechPreset = v;
    pushSpeech();
    emit settingsChanged();
}

void Ft891Bridge::pushSpeech()
{
    SpeechSettings s;
    switch (m_speechPreset) {
    case 1:   // boom headset
        s.enabled = true; s.highPassHz = 300; s.presenceDb = 6;
        s.lowPassHz = 3200; s.compRatio = 3.0; break;
    case 2:   // phone microphone, held at a distance
        s.enabled = true; s.highPassHz = 200; s.presenceDb = 4;
        s.lowPassHz = 3200; s.compRatio = 2.0; break;
    default:  // no processing: let the radio's own PROC and EQ work
        s.enabled = false; s.highPassHz = 0; s.presenceDb = 0;
        s.lowPassHz = 0; s.compRatio = 1.0; break;
    }
    m_cfg.speech = s;
    QMetaObject::invokeMethod(m_core, "setSpeechSettings", Qt::QueuedConnection,
                              Q_ARG(rr::SpeechSettings, s));
}

void Ft891Bridge::setRxGain(double v)
{
    if (qFuzzyCompare(v, double(m_cfg.rxGain))) return;
    m_cfg.rxGain = float(v);
    QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                              Q_ARG(float, m_cfg.rxGain), Q_ARG(float, m_cfg.txGain));
    emit settingsChanged();
}

void Ft891Bridge::setTxGain(double v)
{
    if (qFuzzyCompare(v, double(m_cfg.txGain))) return;
    m_cfg.txGain = float(v);
    QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                              Q_ARG(float, m_cfg.rxGain), Q_ARG(float, m_cfg.txGain));
    emit settingsChanged();
}

void Ft891Bridge::setInputDevice(int v)
{
    if (v == m_cfg.inputDevice) return;
    m_cfg.inputDevice = v;
    QMetaObject::invokeMethod(m_core, "setInputDevice", Qt::QueuedConnection, Q_ARG(int, v));
    emit settingsChanged();
}

void Ft891Bridge::setOutputDevice(int v)
{
    if (v == m_cfg.outputDevice) return;
    m_cfg.outputDevice = v;
    QMetaObject::invokeMethod(m_core, "setOutputDevice", Qt::QueuedConnection, Q_ARG(int, v));
    emit settingsChanged();
}

void Ft891Bridge::setPttOnVolumeKey(bool v)
{
    if (v == m_pttOnVolumeKey) return;
    m_pttOnVolumeKey = v;
    emit settingsChanged();
}

void Ft891Bridge::setRigctldEnabled(bool v)
{
    if (v == m_rigctldEnabled) return;
    m_rigctldEnabled = v;
    if (v) {
        if (!m_rigctld) {
            m_rigctld = new RigctldServer(this);
            connect(m_rigctld, &RigctldServer::logMessage, this, &Ft891Bridge::appendLog);
            connect(m_rigctld, &RigctldServer::requestPtt, this, &Ft891Bridge::setPtt);
            connect(m_rigctld, &RigctldServer::requestCommand, this,
                    [this](const QString &name, const QVariantMap &args) {
                        QMetaObject::invokeMethod(m_core, "command", Qt::QueuedConnection,
                                                  Q_ARG(QString, name), Q_ARG(QVariantMap, args));
                    });
        }
        m_rigctld->updateState(m_state);
        QVariantMap all;
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) all.insert(it.key(), it.value());
        m_rigctld->updateValues(all);
        if (!m_rigctld->start(quint16(rigctldPort()), true)) m_rigctldEnabled = false;
    } else if (m_rigctld) {
        m_rigctld->stop();
    }
    emit settingsChanged();
}

void Ft891Bridge::loadSettings()
{
    QSettings s;
    m_cfg.host      = s.value(QStringLiteral("host"), QStringLiteral("192.168.1.10")).toString();
    m_cfg.tcpPort   = quint16(s.value(QStringLiteral("port"), 7300).toInt());
    m_cfg.udpPort   = quint16(s.value(QStringLiteral("udpPort"), 0).toInt());
    m_cfg.password  = s.value(QStringLiteral("password")).toString();
    m_cfg.encrypt   = s.value(QStringLiteral("encrypt"), true).toBool();
    m_cfg.autoReconnect = s.value(QStringLiteral("autoReconnect"), true).toBool();
    m_cfg.codec     = s.value(QStringLiteral("codec"), QStringLiteral("opus")).toString();
    m_cfg.jitterMs  = s.value(QStringLiteral("jitterMs"), 60).toInt();
    m_cfg.rxGain    = float(s.value(QStringLiteral("rxGain"), 1.0).toDouble());
    m_cfg.txGain    = float(s.value(QStringLiteral("txGain"), 1.0).toDouble());
    m_cfg.inputDevice  = s.value(QStringLiteral("inputDevice"), AudioEngine::defaultInput()).toInt();
    m_cfg.outputDevice = s.value(QStringLiteral("outputDevice"), AudioEngine::defaultOutput()).toInt();
    m_speechPreset  = s.value(QStringLiteral("speechPreset"), 1).toInt();
    m_pttOnVolumeKey = s.value(QStringLiteral("pttOnVolumeKey"), false).toBool();
    m_rigctldEnabled = s.value(QStringLiteral("rigctld"), false).toBool();
    m_stepIndex     = qBound(0, s.value(QStringLiteral("stepIndex"), 1).toInt(), kStepCount - 1);
    m_myCall        = s.value(QStringLiteral("myCall")).toString();
    const QStringList macros = s.value(QStringLiteral("cwMacros")).toStringList();
    for (int i = 0; i < macros.size() && i < m_macros.size(); ++i) m_macros[i] = macros.at(i);
}

void Ft891Bridge::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("host"), m_cfg.host);
    s.setValue(QStringLiteral("port"), m_cfg.tcpPort);
    s.setValue(QStringLiteral("udpPort"), m_cfg.udpPort);
    s.setValue(QStringLiteral("password"), m_cfg.password);
    s.setValue(QStringLiteral("encrypt"), m_cfg.encrypt);
    s.setValue(QStringLiteral("autoReconnect"), m_cfg.autoReconnect);
    s.setValue(QStringLiteral("codec"), m_cfg.codec);
    s.setValue(QStringLiteral("jitterMs"), m_cfg.jitterMs);
    s.setValue(QStringLiteral("rxGain"), double(m_cfg.rxGain));
    s.setValue(QStringLiteral("txGain"), double(m_cfg.txGain));
    s.setValue(QStringLiteral("inputDevice"), m_cfg.inputDevice);
    s.setValue(QStringLiteral("outputDevice"), m_cfg.outputDevice);
    s.setValue(QStringLiteral("speechPreset"), m_speechPreset);
    s.setValue(QStringLiteral("pttOnVolumeKey"), m_pttOnVolumeKey);
    s.setValue(QStringLiteral("rigctld"), m_rigctldEnabled);
    s.setValue(QStringLiteral("stepIndex"), m_stepIndex);
    s.setValue(QStringLiteral("myCall"), m_myCall);
    s.setValue(QStringLiteral("cwMacros"), m_macros);
}

} // namespace rr
