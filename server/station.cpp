#include "station.h"

#include "../common/audioengine.h"

#include <QSettings>

namespace rr {

// ================================================================ settings
QString settingsOrganisation() { return QStringLiteral("FT891Remote"); }
QString settingsApplication()  { return QStringLiteral("FT891RemoteServer"); }

QString legacyDeviceName(const QString &name)
{
    return QString::fromLocal8Bit(name.toUtf8());
}

// The device's name as the list shows it now, for a name saved garbled by an
// earlier version: the window then finds it in its lists, and the next save
// stores it right.
static QString currentDeviceName(const QString &saved, bool input)
{
    if (saved.isEmpty()) return saved;
    const QList<AudioDevice> devices = input ? AudioEngine::inputDevices() : AudioEngine::outputDevices();
    for (const AudioDevice &d : devices)
        if (d.name == saved) return saved;
    for (const AudioDevice &d : devices)
        if (legacyDeviceName(d.name) == saved) return d.name;
    return saved;
}

int audioDeviceByName(const QString &wanted, bool input, int hostApi)
{
    if (wanted.isEmpty())
        return input ? AudioEngine::defaultInput() : AudioEngine::defaultOutput();

    const QList<AudioDevice> devices = input ? AudioEngine::inputDevices(hostApi)
                                             : AudioEngine::outputDevices(hostApi);
    for (const AudioDevice &d : devices)
        if (d.name == wanted) return d.index;
    // A name saved by an earlier version, which read PortAudio's UTF-8 in the
    // local code page: on Windows an accented name was saved garbled.
    for (const AudioDevice &d : devices)
        if (legacyDeviceName(d.name) == wanted) return d.index;
    // Windows appends the host API or a number to a name that returns after
    // being unplugged: a partial match is better than nothing.
    for (const AudioDevice &d : devices)
        if (d.name.contains(wanted, Qt::CaseInsensitive)) return d.index;
    return -1;
}

static Ft891Config::PttMethod pttFromName(const QString &n)
{
    const QString u = n.trimmed().toUpper();
    if (u == QLatin1String("RTS"))   return Ft891Config::PttRts;
    if (u == QLatin1String("DTR"))   return Ft891Config::PttDtr;
    if (u == QLatin1String("NONE"))  return Ft891Config::PttNone;
    // CAT, and anything unknown — the CM108 method of versions up to 0.1.3
    // included: the radio's own TX command always works.
    return Ft891Config::PttCat;
}

static QString pttName(Ft891Config::PttMethod m)
{
    switch (m) {
    case Ft891Config::PttRts:   return QStringLiteral("RTS");
    case Ft891Config::PttDtr:   return QStringLiteral("DTR");
    case Ft891Config::PttNone:  return QStringLiteral("NONE");
    default:                    return QStringLiteral("CAT");
    }
}

StationConfig loadStationConfig(QSettings &s)
{
    StationConfig c;
    Ft891Config &r = c.radio;
    r.catPort        = portNameOf(s.value(QStringLiteral("catPort")).toString());
    r.catBaud        = s.value(QStringLiteral("catBaud"), 38400).toInt();
    r.ptt            = pttFromName(s.value(QStringLiteral("pttMethod"), QStringLiteral("CAT")).toString());
    r.pttPort        = portNameOf(s.value(QStringLiteral("pttPort")).toString());
    r.pollMs         = qBound(50, s.value(QStringLiteral("pollMs"), 200).toInt(), 2000);
    r.readTimeoutMs  = qBound(100, s.value(QStringLiteral("readTimeoutMs"), 400).toInt(), 3000);
    r.powerOnAtStart = s.value(QStringLiteral("powerOnAtStart"), false).toBool();
    r.logTraffic     = s.value(QStringLiteral("logTraffic"), false).toBool();

    ServerConfig &n = c.server;
    n.tcpPort   = quint16(s.value(QStringLiteral("tcpPort"), 7300).toInt());
    n.udpPort   = quint16(s.value(QStringLiteral("udpPort"), 7301).toInt());
    n.password  = s.value(QStringLiteral("password")).toString();
    n.requireEncryption = s.value(QStringLiteral("forceEnc"), false).toBool();
    n.framesPerBuffer   = s.value(QStringLiteral("framesPerBuffer"), 480).toInt();
    n.rxGain    = float(s.value(QStringLiteral("rxGain"), 1.0).toDouble());
    n.txGain    = float(s.value(QStringLiteral("txGain"), 1.0).toDouble());
    n.pttTailMs = s.value(QStringLiteral("tailMs"), 120).toInt();
    n.enforceBandEdges = s.value(QStringLiteral("bandEdges"), true).toBool();
    n.region    = qBound(1, s.value(QStringLiteral("region"), 1).toInt(), 3);

    CwKeyerConfig &k = c.keyer;
    k.enabled      = s.value(QStringLiteral("cwEnable"), false).toBool();
    k.port         = portNameOf(s.value(QStringLiteral("cwPort")).toString());
    k.line         = s.value(QStringLiteral("cwLine"), QStringLiteral("DTR")).toString();
    k.inverted     = s.value(QStringLiteral("cwInvert"), false).toBool();
    k.correctionMs = s.value(QStringLiteral("cwCorr"), 0).toInt();
    k.holdPtt      = s.value(QStringLiteral("cwHoldPtt"), false).toBool();
    k.wpm          = s.value(QStringLiteral("cwWpm"), 20).toInt();

    c.audioIn  = currentDeviceName(s.value(QStringLiteral("audioIn")).toString(), true);
    c.audioOut = currentDeviceName(s.value(QStringLiteral("audioOut")).toString(), false);
    c.hostApi  = s.value(QStringLiteral("hostApi")).toString();
    return c;
}

void saveStationConfig(QSettings &s, const StationConfig &c)
{
    // Keys of settings removed since: CM108 PTT and the PTT tone (0.1.4),
    // the client's raw CAT (0.1.18).
    for (const char *gone : {"cm108Path", "cm108Gpio", "pttTone", "pttToneHz", "allowRawCat"})
        s.remove(QLatin1String(gone));

    const Ft891Config &r = c.radio;
    s.setValue(QStringLiteral("catPort"), r.catPort);
    s.setValue(QStringLiteral("catBaud"), r.catBaud);
    s.setValue(QStringLiteral("pttMethod"), pttName(r.ptt));
    s.setValue(QStringLiteral("pttPort"), r.pttPort);
    s.setValue(QStringLiteral("pollMs"), r.pollMs);
    s.setValue(QStringLiteral("readTimeoutMs"), r.readTimeoutMs);
    s.setValue(QStringLiteral("powerOnAtStart"), r.powerOnAtStart);
    s.setValue(QStringLiteral("logTraffic"), r.logTraffic);

    const ServerConfig &n = c.server;
    s.setValue(QStringLiteral("tcpPort"), n.tcpPort);
    s.setValue(QStringLiteral("udpPort"), n.udpPort);
    s.setValue(QStringLiteral("password"), n.password);
    s.setValue(QStringLiteral("forceEnc"), n.requireEncryption);
    s.setValue(QStringLiteral("framesPerBuffer"), n.framesPerBuffer);
    s.setValue(QStringLiteral("rxGain"), double(n.rxGain));
    s.setValue(QStringLiteral("txGain"), double(n.txGain));
    s.setValue(QStringLiteral("tailMs"), n.pttTailMs);
    s.setValue(QStringLiteral("bandEdges"), n.enforceBandEdges);
    s.setValue(QStringLiteral("region"), n.region);

    const CwKeyerConfig &k = c.keyer;
    s.setValue(QStringLiteral("cwEnable"), k.enabled);
    s.setValue(QStringLiteral("cwPort"), k.port);
    s.setValue(QStringLiteral("cwLine"), k.line);
    s.setValue(QStringLiteral("cwInvert"), k.inverted);
    s.setValue(QStringLiteral("cwCorr"), k.correctionMs);
    s.setValue(QStringLiteral("cwHoldPtt"), k.holdPtt);
    s.setValue(QStringLiteral("cwWpm"), k.wpm);

    s.setValue(QStringLiteral("audioIn"), c.audioIn);
    s.setValue(QStringLiteral("audioOut"), c.audioOut);
    s.setValue(QStringLiteral("hostApi"), c.hostApi);
}

// ================================================================= station
Station::Station(QObject *parent) : QObject(parent)
{
    m_core  = new ServerCore;
    m_radio = new Ft891Controller;
    m_keyer = new CwKeyer;
    m_core->moveToThread(&m_netThread);
    m_radio->moveToThread(&m_radioThread);
    m_keyer->moveToThread(&m_keyerThread);

    // Network to radio: every call is queued, so the network thread never
    // waits on the serial port.
    connect(m_core, &ServerCore::requestPtt,       m_radio, &Ft891Controller::setPtt);
    connect(m_core, &ServerCore::requestCommand,   m_radio, &Ft891Controller::command);
    connect(m_core, &ServerCore::requestTune,      m_radio, &Ft891Controller::startTune);

    // Radio to network.
    connect(m_radio, &Ft891Controller::stateChanged,  m_core, &ServerCore::onRadioState);
    connect(m_radio, &Ft891Controller::valuesChanged, m_core, &ServerCore::onRadioValues);
    connect(m_radio, &Ft891Controller::readProgress,  m_core, &ServerCore::onReadProgress);
    connect(m_radio, &Ft891Controller::notice,        m_core, &ServerCore::onRadioNotice);
    connect(m_radio, &Ft891Controller::morseBusy,     m_core, &ServerCore::onMorseBusy);

    // The local keyer, when enabled, generates the elements next to the
    // radio and keys a serial line; the network never takes part in the
    // spacing. Its own thread: a serial read of the poll must not shift an
    // element.
    connect(m_keyer, &CwKeyer::pttRequested,   m_radio, &Ft891Controller::setPtt);
    connect(m_keyer, &CwKeyer::sendingChanged, m_core,  &ServerCore::onMorseBusy);

    connect(m_core, &ServerCore::requestMorse, this, [this](const QString &text) {
        if (m_cwLocal)
            QMetaObject::invokeMethod(m_keyer, "send", Qt::QueuedConnection, Q_ARG(QString, text));
        else
            QMetaObject::invokeMethod(m_radio, "sendMorse", Qt::QueuedConnection, Q_ARG(QString, text));
    });
    connect(m_core, &ServerCore::requestMorseStop, this, [this] {
        QMetaObject::invokeMethod(m_keyer, "stop", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_radio, "stopMorse", Qt::QueuedConnection);
    });
    connect(m_core, &ServerCore::requestKeySpeed, this, [this](int wpm) {
        QMetaObject::invokeMethod(m_keyer, "setWpm", Qt::QueuedConnection, Q_ARG(int, wpm));
        QMetaObject::invokeMethod(m_radio, "setKeySpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
    });

    // To the front end (window or daemon).
    connect(m_core,  &ServerCore::logMessage,      this, &Station::logMessage);
    connect(m_radio, &Ft891Controller::logMessage, this, &Station::logMessage);
    connect(m_keyer, &CwKeyer::logMessage,         this, &Station::logMessage);
    connect(m_core,  &ServerCore::started,         this, &Station::started);
    connect(m_core,  &ServerCore::clientChanged,   this, &Station::clientChanged);
    connect(m_core,  &ServerCore::statsUpdated,    this, &Station::statsUpdated);
    connect(m_radio, &Ft891Controller::opened,        this, &Station::radioOpened);
    connect(m_radio, &Ft891Controller::stateChanged,  this, &Station::radioState);
    connect(m_radio, &Ft891Controller::valuesChanged, this, &Station::radioValues);
    connect(m_radio, &Ft891Controller::catReply,      this, &Station::catReply);

    m_netThread.start(QThread::TimeCriticalPriority);
    m_radioThread.start(QThread::HighPriority);
    m_keyerThread.start(QThread::TimeCriticalPriority);
}

Station::~Station()
{
    stop();
    m_keyerThread.quit(); m_keyerThread.wait(2000);
    m_netThread.quit();   m_netThread.wait(2000);
    m_radioThread.quit(); m_radioThread.wait(2000);
    delete m_keyer;
    delete m_core;
    delete m_radio;
}

bool Station::start(const StationConfig &cfg, QString *error)
{
    stop();

    int api = -1;
    if (!cfg.hostApi.isEmpty()) {
        for (const auto &h : AudioEngine::hostApis())
            if (h.second == cfg.hostApi) { api = h.first; break; }
    }
    ServerConfig sc = cfg.server;
    sc.inputDevice  = audioDeviceByName(cfg.audioIn, true, api);
    sc.outputDevice = audioDeviceByName(cfg.audioOut, false, api);
    if (sc.inputDevice < 0 || sc.outputDevice < 0) {
        if (error) {
            *error = sc.inputDevice < 0
                ? tr("Audio input not found: %1").arg(cfg.audioIn)
                : tr("Audio output not found: %1").arg(cfg.audioOut);
        }
        return false;
    }

    m_cwLocal = cfg.keyer.enabled;
    QMetaObject::invokeMethod(m_keyer, "open",  Qt::QueuedConnection, Q_ARG(rr::CwKeyerConfig, cfg.keyer));
    QMetaObject::invokeMethod(m_radio, "open",  Qt::QueuedConnection, Q_ARG(rr::Ft891Config, cfg.radio));
    QMetaObject::invokeMethod(m_core,  "start", Qt::QueuedConnection, Q_ARG(rr::ServerConfig, sc));
    m_running = true;
    return true;
}

void Station::stop()
{
    // The keyer first: it may be holding the key line down. Then the network,
    // which releases the PTT, then the radio link.
    QMetaObject::invokeMethod(m_keyer, "close", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_core,  "stop",  Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_radio, "close", Qt::BlockingQueuedConnection);
    m_running = false;
}

void Station::sendCat(const QString &frame, bool expectReply)
{
    QMetaObject::invokeMethod(m_radio, "sendCatString", Qt::QueuedConnection,
                              Q_ARG(QString, frame), Q_ARG(bool, expectReply));
}

void Station::setGains(float rx, float tx)
{
    QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                              Q_ARG(float, rx), Q_ARG(float, tx));
}

} // namespace rr
