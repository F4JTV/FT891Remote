#include "ft891controller.h"

#include "../common/ft891.h"
#include "ft891link.h"

#include <QDateTime>
#include <QSerialPort>
#include <QTimer>

namespace rr {

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Length of a text in Morse units (PARIS timing): a dot is one unit, a dash
// three, one unit between elements, three between letters, seven between
// words. Used to know when the radio will have finished a keyer memory.
int morseUnits(const QString &text)
{
    static const QHash<QChar, QString> table = {
        {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
        {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
        {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
        {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
        {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
        {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"},
        {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."},
        {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'.', ".-.-.-"},
        {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."}, {'=', "-...-"},
        {'+', ".-.-."}, {'-', "-....-"},
    };
    int units = 0;
    for (const QChar ch : text) {
        if (ch == QLatin1Char(' ')) { units += 4; continue; }   // 7, less the 3 already counted
        const QString code = table.value(ch.toUpper());
        if (code.isEmpty()) continue;
        for (const QChar e : code) units += (e == QLatin1Char('-')) ? 3 : 1;
        units += code.size() - 1;   // gaps inside the letter
        units += 3;                 // gap after the letter
    }
    return units;
}

QByteArray readPrefixOf(const QString &readTemplate)
{
    QByteArray p = readTemplate.toLatin1();
    while (p.endsWith(';')) p.chop(1);
    return p;
}

} // namespace

// ================================================================= set-up
Ft891Controller::Ft891Controller(QObject *parent) : QObject(parent)
{
    m_link = new Ft891Link(this);
    connect(m_link, &Ft891Link::frameReceived, this, &Ft891Controller::onFrame);
    connect(m_link, &Ft891Link::requestFailed, this, &Ft891Controller::onFailed);
    connect(m_link, &Ft891Link::linkError,     this, &Ft891Controller::onLinkError);
    connect(m_link, &Ft891Link::frameSent, this, [this](const QByteArray &f) {
        if (m_cfg.logTraffic) emit logMessage(QStringLiteral("→ %1").arg(QString::fromLatin1(f)));
    });

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &Ft891Controller::onPollTick);

    // Changes are gathered for a moment before they leave: one poll cycle
    // touches several fields, and the client needs one update, not five.
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(50);
    connect(m_flushTimer, &QTimer::timeout, this, &Ft891Controller::onFlush);

    m_reopenTimer = new QTimer(this);
    m_reopenTimer->setInterval(3000);
    connect(m_reopenTimer, &QTimer::timeout, this, [this] {
        if (m_closing || m_link->isOpen()) { m_reopenTimer->stop(); return; }
        if (openLink()) emit logMessage(tr("CAT port %1 is back").arg(m_cfg.catPort));
    });

    m_morseTimer = new QTimer(this);
    m_morseTimer->setSingleShot(true);
    connect(m_morseTimer, &QTimer::timeout, this, &Ft891Controller::onMorseChunkDone);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(250);
    connect(m_progressTimer, &QTimer::timeout, this, &Ft891Controller::updateProgress);

    // What the front panel shows is kept fresh in rotation, one setting per
    // poll cycle: someone may turn a knob on the radio itself.
    for (const CatGroup &g : ft891::description().groups) {
        if (g.poll != QLatin1String("panel")) continue;
        for (const CatCommand &c : g.commands)
            if (c.canRead() && !c.params.isEmpty()) m_panelCodes << c.code;
    }
}

Ft891Controller::~Ft891Controller() { close(); }

void Ft891Controller::open(const rr::Ft891Config &cfg)
{
    close();
    m_closing = false;
    m_cfg = cfg;
    m_link->setReadTimeout(qBound(100, cfg.readTimeoutMs, 3000));
    m_state = Ft891State();
    m_published = Ft891State();
    m_state.portName = cfg.catPort;
    m_values.clear();
    m_delta.clear();
    m_pttWanted = false;

    // The PTT hardware first: a line left asserted by a previous program
    // would key the radio as soon as it answers.
    switch (cfg.ptt) {
    case Ft891Config::PttRts:
    case Ft891Config::PttDtr:
        if (!cfg.pttPort.isEmpty() && cfg.pttPort != cfg.catPort) {
            m_pttSerial = new QSerialPort(this);
            m_pttSerial->setPortName(cfg.pttPort);
            if (m_pttSerial->open(QIODevice::ReadWrite)) {
                m_pttSerial->setRequestToSend(false);
                m_pttSerial->setDataTerminalReady(false);
                emit logMessage(tr("PTT on %1, %2 line").arg(cfg.pttPort,
                    cfg.ptt == Ft891Config::PttRts ? QStringLiteral("RTS") : QStringLiteral("DTR")));
            } else {
                emit logMessage(tr("PTT port %1 cannot be opened: %2")
                                    .arg(cfg.pttPort, m_pttSerial->errorString()));
                delete m_pttSerial;
                m_pttSerial = nullptr;
            }
        } else if (cfg.ptt == Ft891Config::PttRts) {
            emit logMessage(tr("Note: RTS on the CAT port also serves menu 05-08 CAT RTS. "
                               "With it enabled the radio stops answering while the PTT is "
                               "released: key through the second, Standard COM port instead."));
        }
        break;
    default:
        break;
    }

    const bool ok = openLink();
    if (!ok) {
        emit logMessage(tr("CAT: %1 — retrying every 3 s").arg(m_state.error));
        m_reopenTimer->start();
    }
    emit opened(ok, ok ? tr("CAT port %1 open").arg(cfg.catPort) : m_state.error);
    markDirty();
}

bool Ft891Controller::openLink()
{
    QString err;
    if (!m_link->open(m_cfg.catPort, m_cfg.catBaud, &err)) {
        m_state.linkOpen = false;
        m_state.error = err;
        markDirty();
        return false;
    }
    m_state.linkOpen = true;
    m_state.error.clear();
    if ((m_cfg.ptt == Ft891Config::PttRts || m_cfg.ptt == Ft891Config::PttDtr) && !m_pttSerial)
        applyLinePtt(false);
    m_reopenTimer->stop();
    emit logMessage(tr("CAT port %1 open at %2 baud").arg(m_cfg.catPort).arg(m_cfg.catBaud));
    startup();
    m_pollTimer->start(qMax(50, m_cfg.pollMs));
    markDirty();
    return true;
}

void Ft891Controller::startup()
{
    m_ifFailures = 0;
    m_quietUntil = nowMs() + 1500;
    m_tick = 0;
    if (m_cfg.powerOnAtStart) powerOn();
    // Auto information off: the server reads the radio on its own schedule,
    // and unsolicited frames would only add traffic to a busy bus.
    m_link->submit("AI0;", {}, Ft891Link::User, QStringLiteral("set:AI"));
    m_link->submit("ID;", "ID", Ft891Link::User, QStringLiteral("rd:ID"));
    m_link->submit("IF;", "IF", Ft891Link::User, QStringLiteral("p:IF"));
}

void Ft891Controller::close()
{
    m_closing = true;
    m_pollTimer->stop();
    m_reopenTimer->stop();
    m_progressTimer->stop();
    // Memory reads die with the link.
    m_memQueue.clear();
    m_memInflight.clear();
    m_memBulkLeft = 0;
    ++m_memGen;
    finishMorse();

    // Unkey whatever the method, even if the server thinks it is in receive.
    if (m_link->isOpen() && m_cfg.ptt == Ft891Config::PttCat) m_link->writeNow("TX0;");
    // A keyer switched off to stop a message is switched back on before the
    // port closes.
    if (m_keyerRestorePending) {
        m_keyerRestorePending = false;
        if (m_link->isOpen()) m_link->writeNow("KR1;");
    }
    if (m_cfg.ptt == Ft891Config::PttRts || m_cfg.ptt == Ft891Config::PttDtr) applyLinePtt(false);
    m_pttWanted = false;

    m_link->close();
    if (m_pttSerial) {
        m_pttSerial->setRequestToSend(false);
        m_pttSerial->setDataTerminalReady(false);
        m_pttSerial->close();
        delete m_pttSerial;
        m_pttSerial = nullptr;
    }
    m_rawExpect.clear();
    m_bulkTotal = 0;
    m_pendingBand = -1;
    m_values.clear();
    m_delta.clear();
    m_lastModeOfFamily.clear();
    m_state = Ft891State();
    m_published = m_state;
    emit stateChanged(m_state);
}

// ================================================================ polling
void Ft891Controller::onPollTick()
{
    if (!m_link->isOpen()) return;
    ++m_tick;

    if (!m_state.radioOn) {
        // One probe a second is enough to notice the radio coming back, and
        // keeps the bus quiet while it is off.
        const int every = qMax(1, 1000 / qMax(50, m_cfg.pollMs));
        if (m_tick % every == 0)
            m_link->submit("IF;", "IF", Ft891Link::Poll, QStringLiteral("p:IF"));
        return;
    }

    // The radio is not keeping up: skip a cycle rather than pile them up.
    if (m_link->queued(Ft891Link::Poll) > 3) return;

    m_link->submit("IF;", "IF", Ft891Link::Poll, QStringLiteral("p:IF"));
    m_link->submit("TX;", "TX", Ft891Link::Poll, QStringLiteral("p:TX"));

    // The S-meter means nothing in transmit, and the transmit meters nothing
    // in receive. SWR comes back every other time: it is the one that matters.
    if (m_state.ptt) {
        static const char *const txMeters[] = {"RM5", "RM6", "RM4", "RM6", "RM3", "RI0"};
        const QByteArray m = txMeters[m_txMeterPos++ % 6];
        m_link->submit(m + ';', m, Ft891Link::Poll, QStringLiteral("p:") + QString::fromLatin1(m));
    } else {
        m_link->submit("SM0;", "SM0", Ft891Link::Poll, QStringLiteral("p:SM0"));
    }

    if (m_tick % 4 == 0) {
        m_link->submit("FB;", "FB", Ft891Link::Poll, QStringLiteral("p:FB"));
    } else if (!m_panelCodes.isEmpty()) {
        const QString code = m_panelCodes.at(m_panelPos++ % m_panelCodes.size());
        if (const CatCommand *c = ft891::command(code))
            submitRead(*c, Ft891Link::Poll, QStringLiteral("p:"));
    }
}

void Ft891Controller::submitRead(const CatCommand &c, int prio, const QString &keyPrefix)
{
    if (!c.canRead()) return;
    m_link->submit(c.readTemplate.toLatin1(), readPrefixOf(c.readTemplate),
                   Ft891Link::Priority(prio), keyPrefix + c.code);
}

// ================================================================ answers
void Ft891Controller::onFrame(const QByteArray &frame)
{
    const QString f = QString::fromLatin1(frame);
    if (m_cfg.logTraffic) emit logMessage(QStringLiteral("← %1;").arg(f));

    m_ifFailures = 0;
    if (!m_state.radioOn) setRadioOn(true);

    // An answer to a frame typed by the operator goes back to him as well.
    for (int i = 0; i < m_rawExpect.size(); ++i) {
        if (frame.startsWith(m_rawExpect.at(i))) {
            emit catReply(f + QLatin1Char(';'));
            m_rawExpect.removeAt(i);
            break;
        }
    }

    // A memory: "MR", a channel, then the fields of IF; frequency 0 is an
    // empty channel. The FT-891 does not write the channel read in that
    // channel field but its current memory channel: MR002 is answered
    // "MR001007100100…" — the fields of 002 — when the radio stands on 001.
    // Filed by that field, every memory landed on the current channel. The
    // memories are read one at a time: the answer belongs to the read in
    // progress, and is filed, channel field corrected, under the channel
    // asked for.
    if (f.startsWith(QLatin1String("MR")) && f.size() >= 5) {
        if (m_memInflight.isEmpty()) {
            // Not asked for, or late for a read given up: whose it is
            // cannot be known.
            if (m_cfg.logTraffic) emit logMessage(tr("Memory answer not asked for: ignored"));
            return;
        }
        const QString ch = m_memInflight;
        const QString fields = ch + f.mid(5);
        const ft891::IfInfo m = ft891::parseIf(QStringLiteral("IF") + fields);
        storeValue(ft891::memoryKey(ch), m.ok && m.freq > 0 ? fields : QString(QLatin1String("")));
        finishMemoryRead();
        return;
    }

    if (f.startsWith(QLatin1String("IF"))) {
        const ft891::IfInfo i = ft891::parseIf(f);
        if (!i.ok) return;
        m_state.freqA = i.freq;
        if (const ft891::ModeInfo *m = ft891::modeByCode(i.mode)) noteMode(m->name);
        m_state.memMode = ft891::memModeName(i.memMode);
        m_state.memChannel = i.memChannel;
        m_state.memName = i.memName;
        m_state.clarOffset = i.clarOffset;
        // IF only says the clarifier is on; menu 05-18 says which side it
        // moves. The band-edge guard needs the transmit side.
        const QString sel = m_values.value(QStringLiteral("EX0518"));
        m_state.rxClar = i.clarOn && ft891::clarMovesRx(sel);
        m_state.txClar = i.clarOn && ft891::clarMovesTx(sel);
        m_state.toneMode = i.tone;
        m_state.rptShift = i.shift;
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("FA"))) {
        // In memory mode FA still holds the VFO: the display follows IF.
        if (m_state.memMode == QLatin1String("VFO")) m_state.freqA = f.mid(2).toULongLong();
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("FB"))) {
        m_state.freqB = f.mid(2).toULongLong();
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("MD0")) && f.size() >= 4) {
        if (const ft891::ModeInfo *m = ft891::modeByCode(f.at(3))) noteMode(m->name);
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("TX")) && f.size() == 3) {
        const bool tx = f.at(2) != QLatin1Char('0');
        if (tx && !m_state.ptt) { m_state.po = m_state.swr = m_state.alc = m_state.comp = 0; }
        m_state.ptt = tx;
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("SM0"))) {
        m_state.sMeter = f.mid(3, 3).toInt();
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("RM")) && f.size() >= 6) {
        const int v = f.mid(3, 3).toInt();
        switch (f.at(2).toLatin1()) {
        case '3': m_state.comp = v; break;
        case '4': m_state.alc = v;  break;
        case '5': m_state.po = v;   break;
        case '6': m_state.swr = v;  break;
        default: break;
        }
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("RI0")) && f.size() >= 4) {
        m_state.hiSwr = f.at(3) == QLatin1Char('1');
        markDirty();
        return;
    }
    if (f.startsWith(QLatin1String("ID"))) {
        const QString id = f.mid(2);
        if (id != m_state.radioId) {
            m_state.radioId = id;
            if (id == QLatin1String("0650"))
                emit logMessage(tr("FT-891 identified (ID %1)").arg(id));
            else
                emit logMessage(tr("The radio answers ID %1, not 0650: this may not be an "
                                   "FT-891. Continuing, but settings may not match.").arg(id));
            markDirty();
        }
        return;
    }
    if (f.startsWith(QLatin1String("PS")) && f.size() == 3) {
        storeValue(QStringLiteral("PS"), f.mid(2));
        return;
    }

    QString value;
    const CatCommand *c = ft891::matchAnswer(f, &value);
    if (!c) return;
    const CatGroup *g = ft891::groupOf(c->code);
    if (g && g->poll == QLatin1String("meter")) return;
    storeValue(c->code, value);
}

void Ft891Controller::onFailed(const QByteArray &frame, bool refused)
{
    const QString f = QString::fromLatin1(frame);

    for (int i = 0; i < m_rawExpect.size(); ++i) {
        if (frame.startsWith(m_rawExpect.at(i))) {
            emit catReply(refused ? QStringLiteral("?;") : QString());
            m_rawExpect.removeAt(i);
            break;
        }
    }

    if (!refused) {
        // Silence on the frame that is always answered means the radio is
        // off, or the port or speed is wrong. Three in a row before saying so.
        if (f.startsWith(QLatin1String("IF")) && ++m_ifFailures >= 3
            && nowMs() > m_quietUntil && m_state.radioOn)
            setRadioOn(false);
        return;
    }

    // An empty memory channel is refused: it is a value, not a fault. No
    // answer at all is read again.
    if (f.startsWith(QLatin1String("MR")) && f.size() >= 5) {
        const QString ch = f.mid(2, 3);
        if (ch != m_memInflight) return;     // an earlier read, given up
        if (refused) {
            storeValue(ft891::memoryKey(ch), QString(QLatin1String("")));
            finishMemoryRead();
        } else {
            retryMemoryRead(tr("no answer"));
        }
        return;
    }
    if (f.startsWith(QLatin1String("MW"))) {
        emit notice(tr("The radio refused to write memory %1").arg(f.mid(2, 3)));
        return;
    }

    if (f.startsWith(QLatin1String("BS")) && m_pendingBand >= 0) {
        const ft891::BandInfo b = ft891::bands().value(m_pendingBand);
        m_pendingBand = -1;
        if (b.preset > 0) {
            emit logMessage(tr("Band stack refused — tuning %1 m to %2 kHz instead")
                                .arg(b.name).arg(b.preset / 1000));
            doFrequency(QVariantMap{{QStringLiteral("hz"), double(b.preset)},
                                    {QStringLiteral("vfo"), QStringLiteral("A")}});
        }
        return;
    }

    if ((f.startsWith(QLatin1String("KY")) || f.startsWith(QLatin1String("KM"))) && m_morseActive) {
        emit notice(tr("The radio refused its keyer memory: it must be in CW with the "
                       "keyer on (CW SETTING → KEYER)."));
        finishMorse();
        return;
    }

    // A read refused in rotation (repeater shift outside FM, for instance)
    // would come back every cycle: logged once, then kept quiet.
    static QSet<QString> reported;
    bool isRead = false;
    for (const CatGroup &g : ft891::description().groups)
        for (const CatCommand &c : g.commands)
            if (c.readTemplate == f) { isRead = true; break; }
    if (isRead) {
        if (!reported.contains(f)) {
            reported.insert(f);
            emit logMessage(tr("The radio does not answer %1 in its current state").arg(f));
        }
        return;
    }
    emit logMessage(tr("The radio refused %1").arg(f));
    emit notice(tr("The radio refused %1 — not available in this mode or state?").arg(f));
}

void Ft891Controller::setRadioOn(bool on)
{
    if (m_state.radioOn == on) return;
    m_state.radioOn = on;
    markDirty();
    if (on) {
        emit logMessage(tr("The radio answers"));
        m_link->submit("AI0;", {}, Ft891Link::User, QStringLiteral("set:AI"));
        if (m_state.radioId.isEmpty())
            m_link->submit("ID;", "ID", Ft891Link::User, QStringLiteral("rd:ID"));
        m_link->submit("FB;", "FB", Ft891Link::ReadBack, QStringLiteral("rb:FB"));
        for (const QString &k : {QStringLiteral("FS"), QStringLiteral("MC")})
            if (const CatCommand *c = ft891::command(k))
                submitRead(*c, Ft891Link::ReadBack, QStringLiteral("rb:"));
        // What the front panel shows, and the two settings the server relies
        // on for CW: keyer memory 1 must hold text, and the keyer speed.
        for (const CatGroup &g : ft891::description().groups)
            if (g.poll == QLatin1String("panel")) readGroupNamed(g.name, true);
        // Menu settings the server relies on: keyer memory 1 must hold text
        // for CW, and CLAR SELECT says whether the clarifier moves the
        // transmit frequency the band-edge guard checks.
        for (const QString &k : {QStringLiteral("EX0407"), QStringLiteral("EX0408"), QStringLiteral("EX0409"),
                                 QStringLiteral("EX0410"), QStringLiteral("EX0411"), QStringLiteral("EX0518")})
            if (const CatCommand *c = ft891::command(k))
                submitRead(*c, Ft891Link::ReadBack, QStringLiteral("rb:"));
    } else {
        emit logMessage(tr("The radio does not answer — switched off, or wrong port or "
                           "speed (menu 05-06 CAT RATE must match)?"));
        m_link->dropQueue(Ft891Link::Bulk);
        m_link->dropQueue(Ft891Link::ReadBack);
        m_bulkTotal = 0;
        m_state.ptt = false;
        m_state.sMeter = 0;
        updateProgress();
    }
}

void Ft891Controller::onLinkError(const QString &message)
{
    emit logMessage(tr("CAT port lost: %1 — retrying every 3 s").arg(message));
    m_pollTimer->stop();
    m_state.linkOpen = false;
    m_state.error = message;
    setRadioOn(false);
    m_rawExpect.clear();
    finishMorse();
    markDirty();
    if (!m_closing) m_reopenTimer->start();
}

// =============================================================== publishing
void Ft891Controller::markDirty()
{
    m_stateDirty = true;
    if (!m_flushTimer->isActive()) m_flushTimer->start();
}

void Ft891Controller::storeValue(const QString &code, const QString &value)
{
    const auto it = m_values.constFind(code);
    if (it != m_values.constEnd() && *it == value) return;
    m_values.insert(code, value);
    m_delta.insert(code, value);
    if (!m_flushTimer->isActive()) m_flushTimer->start();
}

void Ft891Controller::onFlush()
{
    if (m_stateDirty && m_state.differs(m_published)) {
        m_published = m_state;
        emit stateChanged(m_state);
    }
    m_stateDirty = false;
    if (!m_delta.isEmpty()) {
        emit valuesChanged(m_delta);
        m_delta.clear();
    }
}

void Ft891Controller::updateProgress()
{
    if (m_bulkTotal <= 0) {
        m_progressTimer->stop();
        emit readProgress(0, 0);
        return;
    }
    const int left = m_link->queued(Ft891Link::Bulk) + m_memBulkLeft;
    emit readProgress(qMax(0, m_bulkTotal - left), m_bulkTotal);
    if (left == 0) {
        m_bulkTotal = 0;
        m_progressTimer->stop();
        emit readProgress(0, 0);
    }
}

void Ft891Controller::readGroupNamed(const QString &group, bool bulk)
{
    for (const CatGroup &g : ft891::description().groups) {
        if (g.name != group) continue;
        for (const CatCommand &c : g.commands) {
            // Settings, and the read-only information of the menu — the
            // three versions of menu 18 have no parameter at all and used to
            // be left out.
            if (!c.canRead() || (c.params.isEmpty() && c.canSet())) continue;
            const QString key = (bulk ? QStringLiteral("bulk:") : QStringLiteral("rb:")) + c.code;
            if (m_link->isQueued(key)) continue;
            submitRead(c, bulk ? Ft891Link::Bulk : Ft891Link::ReadBack,
                       bulk ? QStringLiteral("bulk:") : QStringLiteral("rb:"));
            if (bulk) ++m_bulkTotal;
        }
    }
    if (bulk && m_bulkTotal > 0 && !m_progressTimer->isActive()) {
        m_progressTimer->start();
        updateProgress();
    }
}

void Ft891Controller::readAll()
{
    for (const CatGroup &g : ft891::description().groups) {
        if (g.poll == QLatin1String("core") || g.poll == QLatin1String("meter")) continue;
        readGroupNamed(g.name, true);
    }
    readMemories(true);
}

void Ft891Controller::readMemories(bool bulk)
{
    int added = 0;
    for (const QString &ch : ft891::memoryChannels()) {
        if (m_memQueue.contains(ch) || ch == m_memInflight) continue;
        m_memQueue << ch;
        ++added;
    }
    if (bulk && added > 0) {
        m_bulkTotal += added;
        m_memBulkLeft += added;
        if (!m_progressTimer->isActive()) {
            m_progressTimer->start();
            updateProgress();
        }
    }
    QTimer::singleShot(0, this, &Ft891Controller::readNextMemory);
}

// One channel, ahead of the others: the read-back after a write.
void Ft891Controller::queueMemory(const QString &ch, bool first)
{
    if (ch == m_memInflight) return;
    m_memQueue.removeAll(ch);
    if (first) m_memQueue.prepend(ch); else m_memQueue.append(ch);
    QTimer::singleShot(0, this, &Ft891Controller::readNextMemory);
}

// The memories are read one at a time, each with a long wait for its
// answer: the radio takes longer over MR than over a setting, and an answer
// arriving after the usual 400 ms was taken for the answer to the next read.
void Ft891Controller::readNextMemory()
{
    if (!m_memInflight.isEmpty() || m_memQueue.isEmpty() || !m_link->isOpen()) return;
    m_memInflight = m_memQueue.takeFirst();
    m_memTries = 0;
    sendMemoryRead();
}

void Ft891Controller::sendMemoryRead()
{
    ++m_memGen;
    m_link->submit(QStringLiteral("MR%1;").arg(m_memInflight).toLatin1(), "MR", Ft891Link::ReadBack,
                   QStringLiteral("mem:") + m_memInflight, 1500);
}

void Ft891Controller::finishMemoryRead(bool gaveUp)
{
    m_memInflight.clear();
    ++m_memGen;
    if (m_memBulkLeft > 0) --m_memBulkLeft;
    // A short pause between two memory reads. After a read given up, a long
    // one: an answer still on its way arrives while no read is in progress,
    // and is ignored rather than taken for the next channel's.
    QTimer::singleShot(gaveUp ? 1500 : 20, this, &Ft891Controller::readNextMemory);
}

// No answer: the read is made again, twice at most; then the channel is
// left unknown.
void Ft891Controller::retryMemoryRead(const QString &why)
{
    if (m_memTries++ >= 2) {
        emit logMessage(tr("Memory %1 not read (%2): left unknown").arg(m_memInflight, why));
        finishMemoryRead(true);
        return;
    }
    if (m_cfg.logTraffic) emit logMessage(tr("Memory %1: %2; reading it again").arg(m_memInflight, why));
    const int gen = ++m_memGen;
    QTimer::singleShot(300, this, [this, gen] {
        if (gen == m_memGen && !m_memInflight.isEmpty()) sendMemoryRead();
    });
}

// MW: the fields of IF, from the channel on. Frequency, clarifier, mode,
// CTCSS on/off and repeater shift are all a memory holds over CAT: its tone
// number and its name are not part of it.
void Ft891Controller::doMemoryWrite(const QVariantMap &a)
{
    const QString ch = a.value(QStringLiteral("ch")).toString();
    const qint64 hz = a.value(QStringLiteral("hz")).toLongLong();
    const ft891::ModeInfo *m = ft891::modeByName(a.value(QStringLiteral("mode")).toString());
    const int clar = a.value(QStringLiteral("clar")).toInt();
    const int tone = a.value(QStringLiteral("tone")).toInt();
    const int shift = a.value(QStringLiteral("shift")).toInt();
    QString why;
    if (!ft891::isMemoryChannel(ch)) why = tr("no memory channel %1").arg(ch);
    else if (hz < 30000 || hz > 56000000) why = tr("the FT-891 covers 30 kHz to 56 MHz");
    else if (!m) why = tr("unknown mode");
    else if (clar < -9999 || clar > 9999) why = tr("the clarifier offset is ±9999 Hz");
    else if (tone < 0 || tone > 2 || shift < 0 || shift > 2) why = tr("invalid tone or shift");
    if (!why.isEmpty()) {
        emit notice(tr("Memory not written: %1").arg(why));
        return;
    }
    const QString frame = QStringLiteral("MW") + ch
        + QStringLiteral("%1").arg(hz, 9, 10, QLatin1Char('0'))
        + (clar < 0 ? QLatin1Char('-') : QLatin1Char('+'))
        + QStringLiteral("%1").arg(qAbs(clar), 4, 10, QLatin1Char('0'))
        + (a.value(QStringLiteral("clarOn")).toBool() ? QLatin1Char('1') : QLatin1Char('0'))
        + QLatin1Char('0')                                  // P5, fixed
        + m->code
        + QLatin1Char('0')                                  // P7, fixed
        + QString::number(tone)
        + QStringLiteral("00")                              // P9, fixed
        + QString::number(shift) + QLatin1Char(';');
    m_link->submit(frame.toLatin1(), {}, Ft891Link::User);
    emit logMessage(tr("Memory %1 written: %2 Hz %3").arg(ch).arg(hz).arg(m->name));
    // What the radio kept, not what was sent.
    QTimer::singleShot(200, this, [this, ch] { queueMemory(ch, true); });
}

void Ft891Controller::doMemoryRecall(const QString &ch)
{
    if (!ft891::isMemoryChannel(ch)) return;
    m_link->submit(QStringLiteral("MC%1;").arg(ch).toLatin1(), {}, Ft891Link::User,
                   QStringLiteral("set:MC"));
    QTimer::singleShot(250, this, [this] {
        m_link->submit("IF;", "IF", Ft891Link::ReadBack, QStringLiteral("rb:IF"));
    });
}

// ================================================================ commands
void Ft891Controller::command(const QString &name, const QVariantMap &args)
{
    if (!m_link->isOpen()) {
        emit notice(tr("The CAT port is not open"));
        return;
    }
    if (name == QLatin1String("freq"))       doFrequency(args);
    else if (name == QLatin1String("mode"))  doMode(args.value(QStringLiteral("v")).toString());
    else if (name == QLatin1String("set"))   doSet(args);
    else if (name == QLatin1String("band"))  doBand(args.value(QStringLiteral("index")).toInt());
    else if (name == QLatin1String("clar"))  doClarifier(args.value(QStringLiteral("delta")).toInt());
    else if (name == QLatin1String("power")) {
        if (args.value(QStringLiteral("on")).toBool()) powerOn(); else powerOff();
    }
    else if (name == QLatin1String("memread"))   readMemories(true);
    else if (name == QLatin1String("memwrite"))  doMemoryWrite(args);
    else if (name == QLatin1String("memrecall")) doMemoryRecall(args.value(QStringLiteral("ch")).toString());
    else if (name == QLatin1String("cwmem")) {
        // A memory is played by the command of its type, set in menus 04-07
        // to 04-11: TEXT, written with KM, by KY6-KYA (code KYn); MESSAGE,
        // recorded with the paddle, by KY1-KY5 (code MSGn). The radio
        // refuses the other one with "?".
        const int n = args.value(QStringLiteral("n")).toInt();
        if (n >= 1 && n <= 5) {
            const QString type = m_values.value(QStringLiteral("EX04%1").arg(6 + n, 2, 10, QLatin1Char('0')));
            const bool message = args.contains(QStringLiteral("message"))
                                     ? args.value(QStringLiteral("message")).toBool()
                                     : type == QLatin1String("1");
            doSet(QVariantMap{{QStringLiteral("code"),
                               (message ? QStringLiteral("MSG%1") : QStringLiteral("KY%1")).arg(n)}});
        }
    }
    else if (name == QLatin1String("read")) {
        if (args.value(QStringLiteral("all")).toBool()) {
            readAll();
        } else if (args.contains(QStringLiteral("group"))) {
            readGroupNamed(args.value(QStringLiteral("group")).toString(), true);
        } else {
            const QStringList codes = args.value(QStringLiteral("codes")).toStringList();
            for (const QString &code : codes)
                if (const CatCommand *c = ft891::command(code))
                    submitRead(*c, Ft891Link::ReadBack, QStringLiteral("rb:"));
        }
    }
    else {
        emit notice(tr("Unknown command %1").arg(name));
    }
}

void Ft891Controller::doFrequency(const QVariantMap &args)
{
    const quint64 hz = quint64(args.value(QStringLiteral("hz")).toDouble() + 0.5);
    const bool vfoB = args.value(QStringLiteral("vfo")).toString() == QLatin1String("B");
    if (hz < 30000 || hz > 56000000) {
        emit notice(tr("%1 Hz is outside the FT-891 range (30 kHz – 56 MHz)").arg(hz));
        return;
    }
    const QByteArray frame = QStringLiteral("F%1%2;").arg(vfoB ? 'B' : 'A')
                                 .arg(hz, 9, 10, QLatin1Char('0')).toLatin1();
    // Keyed by VFO: a knob turned quickly sends where it stopped, not every
    // step on the way.
    m_link->submit(frame, {}, Ft891Link::User, vfoB ? QStringLiteral("set:FB") : QStringLiteral("set:FA"));
    if (vfoB) m_link->submit("FB;", "FB", Ft891Link::ReadBack, QStringLiteral("rb:FB"));
    else      m_link->submit("IF;", "IF", Ft891Link::ReadBack, QStringLiteral("rb:IF"));
}

// The mode as the radio reports it — the only one displayed — and the
// variant of its family to come back to.
void Ft891Controller::noteMode(const QString &mode)
{
    m_state.mode = mode;
    const QString family = ft891::modeFamily(mode);
    if (!family.isEmpty()) m_lastModeOfFamily.insert(family, mode);
}

void Ft891Controller::doMode(const QString &name)
{
    // The operator chooses a family, as with the radio's MODE key; data
    // programs going through rigctld still name an exact mode (PKTUSB…).
    QString target = name;
    if (ft891::isModeFamily(name)) {
        const QString family = name.trimmed().toUpper();
        if (ft891::modeFamily(m_state.mode) == family) {
            // Already there: the radio's own choice — LSB or USB, CW-U or
            // CW-L — is kept.
            readBackMode();
            return;
        }
        target = family == QLatin1String("SSB")
                     ? ft891::ssbSidebandFor(m_state.freqA)
                     : m_lastModeOfFamily.value(family, ft891::defaultModeOf(family));
    }
    const ft891::ModeInfo *m = ft891::modeByName(target);
    if (!m) {
        emit notice(tr("Unknown mode %1").arg(name));
        return;
    }
    m_link->submit(QStringLiteral("MD0%1;").arg(m->code).toLatin1(), {}, Ft891Link::User,
                   QStringLiteral("set:MD"));
    if (m_cfg.logTraffic) emit logMessage(tr("Mode %1 → %2").arg(name, m->name));
    readBackMode();
}

// What is displayed is what the radio says, never what was asked: the mode
// is read back at once, then the whole state and the filter width, which
// follows the mode, once the radio has settled.
void Ft891Controller::readBackMode()
{
    m_link->submit("MD0;", "MD", Ft891Link::ReadBack, QStringLiteral("rb:MD"));
    QTimer::singleShot(250, this, [this] {
        m_link->submit("IF;", "IF", Ft891Link::ReadBack, QStringLiteral("rb:IF"));
        if (const CatCommand *w = ft891::command(QStringLiteral("WDH")))
            submitRead(*w, Ft891Link::ReadBack, QStringLiteral("rb:"));
    });
}

void Ft891Controller::doBand(int index)
{
    const QList<ft891::BandInfo> &b = ft891::bands();
    if (index < 0 || index >= b.size()) return;
    if (b.at(index).bs.isEmpty()) {
        // No band-stack key for this band (60 m): its preset frequency.
        doFrequency(QVariantMap{{QStringLiteral("hz"), double(b.at(index).preset)},
                                {QStringLiteral("vfo"), QStringLiteral("A")}});
        return;
    }
    m_link->submit(QStringLiteral("BS%1;").arg(b.at(index).bs).toLatin1(), {},
                   Ft891Link::User, QStringLiteral("set:BS"));
    m_pendingBand = index;
    // A band change takes the radio a moment: read it back once it is done.
    QTimer::singleShot(300, this, [this] {
        if (m_pendingBand >= 0) m_pendingBand = -1;
        m_link->submit("IF;", "IF", Ft891Link::ReadBack, QStringLiteral("rb:IF"));
        m_link->submit("FB;", "FB", Ft891Link::ReadBack, QStringLiteral("rb:FB"));
    });
}

void Ft891Controller::doClarifier(int delta)
{
    if (delta == 0) return;
    const int step = qMin(qAbs(delta), 9999);
    // Relative commands are never merged: two steps up are two steps up.
    m_link->submit(QStringLiteral("%1%2;").arg(delta > 0 ? QStringLiteral("RU") : QStringLiteral("RD"))
                       .arg(step, 4, 10, QLatin1Char('0')).toLatin1(),
                   {}, Ft891Link::User);
    m_link->submit("IF;", "IF", Ft891Link::ReadBack, QStringLiteral("rb:IF"));
}

void Ft891Controller::doSet(const QVariantMap &args)
{
    const QString code = args.value(QStringLiteral("code")).toString();
    const CatCommand *c = ft891::command(code);
    if (!c || !c->canSet()) {
        emit notice(tr("%1 cannot be set").arg(code));
        return;
    }
    // Commands with a path of their own, so that no safeguard is bypassed.
    if (code == QLatin1String("TX") || code == QLatin1String("MOX")) {
        emit notice(tr("Transmit through the PTT: the server watches it, and releases "
                       "it if the link drops."));
        return;
    }
    if (code == QLatin1String("AI")) {
        emit notice(tr("Auto information is managed by the server"));
        return;
    }
    if (!c->confirm.isEmpty() && !args.value(QStringLiteral("confirmed")).toBool()) {
        emit notice(tr("%1 needs a confirmation").arg(c->name));
        return;
    }

    QStringList vals;
    QString why;
    if (!ft891::validateValues(*c, args.value(QStringLiteral("v")).toStringList(), &vals, &why)) {
        emit notice(why);
        return;
    }

    if (code == QLatin1String("PS")) {
        if (vals.value(0) == QLatin1String("1")) powerOn(); else powerOff();
        return;
    }
    if (code == QLatin1String("BS")) {
        const QList<ft891::BandInfo> &b = ft891::bands();
        for (int i = 0; i < b.size(); ++i)
            if (b.at(i).bs == vals.value(0)) { doBand(i); return; }
        return;
    }
    if (code == QLatin1String("TNR") && vals.value(0) == QLatin1String("2")) {
        startTune();
        return;
    }
    if (code == QLatin1String("SPEED")) m_wpm = vals.value(0).toInt();
    if (code == QLatin1String("WDH")) {
        // The steps that exist depend on the mode and on NARROW (the SH table
        // of the CAT reference): anything else is refused here, with the
        // reason, rather than by the radio without one.
        const bool narrow = m_values.value(QStringLiteral("NAR")) == QLatin1String("1");
        const bool widthOn = vals.value(0) == QLatin1String("1");
        const int step = vals.value(1).toInt();
        const ft891::WidthRange wr = ft891::widthRange(m_state.mode, narrow);
        // Off keeps whatever step is stored; on needs one the mode has.
        if (!wr.available || (widthOn && !ft891::widthStepValid(m_state.mode, narrow, step))) {
            const ft891::WidthRange r = ft891::widthRange(m_state.mode, narrow);
            emit notice(r.available
                ? tr("WIDTH step %1 does not exist in %2 with NARROW %3: 00, or %4 to %5")
                      .arg(step, 2, 10, QLatin1Char('0')).arg(m_state.mode, narrow ? tr("on") : tr("off"))
                      .arg(r.first, 2, 10, QLatin1Char('0')).arg(r.last, 2, 10, QLatin1Char('0'))
                : tr("No WIDTH setting in %1").arg(m_state.mode));
            return;
        }
    }

    const QByteArray frame = c->buildSet(vals).toLatin1();
    // Actions are never merged; settings are, by code.
    m_link->submit(frame, {}, Ft891Link::User,
                   c->params.isEmpty() ? QString() : QStringLiteral("set:") + code);
    if (m_cfg.logTraffic) emit logMessage(tr("%1 → %2").arg(c->name, QString::fromLatin1(frame)));
    readBackAfter(*c);
}

void Ft891Controller::readBackAfter(const CatCommand &c)
{
    if (c.params.isEmpty()) {
        // An action — A=B, A/B, band up, QMB recall, V/M — moves the
        // frequencies and the mode: read them once the radio has settled.
        const bool quickSplit = c.code == QLatin1String("QSPL");
        QTimer::singleShot(250, this, [this, quickSplit] {
            m_link->submit("IF;", "IF", Ft891Link::ReadBack, QStringLiteral("rb:IF"));
            m_link->submit("FB;", "FB", Ft891Link::ReadBack, QStringLiteral("rb:FB"));
            // Quick split sets VFO-B and switches split on.
            if (quickSplit)
                if (const CatCommand *w = ft891::command(QStringLiteral("SPL")))
                    submitRead(*w, Ft891Link::ReadBack, QStringLiteral("rb:"));
        });
        return;
    }
    submitRead(c, Ft891Link::ReadBack, QStringLiteral("rb:"));
    // NAR switches the filter to its narrow width: WIDTH changes with it.
    if (c.code == QLatin1String("NAR"))
        if (const CatCommand *w = ft891::command(QStringLiteral("WDH")))
            submitRead(*w, Ft891Link::ReadBack, QStringLiteral("rb:"));
    // Menu 05-05 decides whether the knob is RF gain or squelch.
    if (c.code == QLatin1String("EX0505")) {
        for (const QString &k : {QStringLiteral("RF"), QStringLiteral("SQL")})
            if (const CatCommand *w = ft891::command(k))
                submitRead(*w, Ft891Link::ReadBack, QStringLiteral("rb:"));
    }
}

void Ft891Controller::sendCatString(const QString &text, bool expectReply)
{
    QString s = text.trimmed();
    if (s.isEmpty()) return;
    if (!s.endsWith(QLatin1Char(';'))) s += QLatin1Char(';');
    const QByteArray frame = s.toLatin1();

    // The terminal must not bypass the PTT: a typed TX1; would key the radio
    // with no audio path open and no watchdog to release it.
    if ((frame.startsWith("TX") || frame.startsWith("MX")) && frame.size() > 3) {
        emit catReply(tr("refused by the server: transmit through the PTT"));
        return;
    }
    if (!m_link->isOpen()) {
        emit catReply(tr("the CAT port is not open"));
        return;
    }
    if (expectReply) {
        m_rawExpect.append(frame.left(2));
        m_link->submit(frame, frame.left(2), Ft891Link::User);
    } else {
        m_link->submit(frame, {}, Ft891Link::User);
        // A setting is accepted in silence; a refusal would arrive within
        // a few milliseconds and would already have been reported.
        QTimer::singleShot(400, this, [this] { emit catReply(QString()); });
    }
}

// =================================================================== power
void Ft891Controller::powerOn()
{
    emit logMessage(tr("Switching the radio on"));
    // A sleeping FT-891 ignores the first frame: it only wakes the CAT
    // interface. The second, a second later, is the one it obeys.
    m_link->writeRaw("PS1;");
    QTimer::singleShot(1100, this, [this] { m_link->writeNow("PS1;"); });
    m_quietUntil = nowMs() + 8000;
}

void Ft891Controller::powerOff()
{
    emit logMessage(tr("Switching the radio off"));
    m_link->writeNow("PS0;");
}

// ===================================================================== PTT
void Ft891Controller::applyLinePtt(bool on)
{
    const bool rts = (m_cfg.ptt == Ft891Config::PttRts);
    if (m_pttSerial) {
        if (rts) m_pttSerial->setRequestToSend(on);
        else     m_pttSerial->setDataTerminalReady(on);
    } else {
        if (rts) m_link->setRts(on);
        else     m_link->setDtr(on);
    }
}

void Ft891Controller::setPtt(bool on)
{
    // Keying twice is harmless but pointless; releasing is always carried out,
    // whatever the server believes the state to be.
    if (on && m_pttWanted) return;
    m_pttWanted = on;

    switch (m_cfg.ptt) {
    case Ft891Config::PttCat:
        m_link->writeNow(on ? "TX1;" : "TX0;");
        break;
    case Ft891Config::PttRts:
    case Ft891Config::PttDtr:
        applyLinePtt(on);
        break;
    case Ft891Config::PttNone:
        break;
    }
    // Whatever the method, the radio's own answer is what the display shows.
    m_link->submit("TX;", "TX", Ft891Link::User, QStringLiteral("rb:TX"));
}

void Ft891Controller::startTune()
{
    m_link->submit("AC002;", {}, Ft891Link::User);
    emit logMessage(tr("Tuner cycle started"));
    QTimer::singleShot(400, this, [this] {
        if (const CatCommand *c = ft891::command(QStringLiteral("TNR")))
            submitRead(*c, Ft891Link::ReadBack, QStringLiteral("rb:"));
    });
}

// ===================================================================== CW
int Ft891Controller::currentWpm() const
{
    const int v = m_values.value(QStringLiteral("SPEED")).toInt();
    return v >= 4 ? v : m_wpm;
}

void Ft891Controller::setKeySpeed(int wpm)
{
    m_wpm = qBound(4, wpm, 60);
    doSet(QVariantMap{{QStringLiteral("code"), QStringLiteral("SPEED")},
                      {QStringLiteral("v"), QStringList{QString::number(m_wpm)}}});
}

// Free text through the radio's own keyer: the FT-891 cannot key typed text
// over CAT, but it can play a keyer memory. The text is written into memory
// 1 (KM1) and played with KY6, fifty characters at a time — the size of a
// memory. On the FT-891 a TEXT memory is played by KY6-KYA: KY1-KY5 are
// refused ("?"), whatever the reference's names suggest. The next piece is
// written once the radio has had time to send the previous one.
void Ft891Controller::sendMorse(const QString &text)
{
    const QString t = ft891::keyerText(text);
    if (t.isEmpty()) return;

    QString current;
    const QStringList words = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &w : words) {
        const QString word = w.left(50);
        if (current.isEmpty()) current = word;
        else if (current.size() + 1 + word.size() <= 50) current += QLatin1Char(' ') + word;
        else { m_morseChunks << current; current = word; }
    }
    if (!current.isEmpty()) m_morseChunks << current;
    if (m_morseActive) return;   // appended to the message being sent

    m_morseActive = true;
    emit morseBusy(true);
    // Memory 1 must hold text: in MESSAGE mode it would play a paddle
    // recording instead of what the operator typed.
    if (m_values.value(QStringLiteral("EX0407")) != QLatin1String("0")) {
        m_link->submit("EX04070;", {}, Ft891Link::User);
        emit logMessage(tr("Menu 04-07 CW MEMORY 1 set to TEXT, so that typed text can be sent"));
        if (const CatCommand *c = ft891::command(QStringLiteral("EX0407")))
            submitRead(*c, Ft891Link::ReadBack, QStringLiteral("rb:"));
    }
    nextMorseChunk();
}

void Ft891Controller::nextMorseChunk()
{
    if (m_morseChunks.isEmpty()) { finishMorse(); return; }
    const QString chunk = m_morseChunks.takeFirst();
    // No coalescing key: each piece must go out, in order.
    m_link->submit(("KM1" + chunk + ';').toLatin1(), {}, Ft891Link::User);
    m_link->submit("KY6;", {}, Ft891Link::User);
    emit logMessage(tr("Keying: %1").arg(chunk));

    const double ditMs = 1200.0 / currentWpm();
    // A margin for weight settings above 3.0 and for the break-in delay.
    const int ms = int(morseUnits(chunk) * ditMs * 1.15) + 600;
    m_morseTimer->start(ms);
}

void Ft891Controller::onMorseChunkDone() { nextMorseChunk(); }

void Ft891Controller::finishMorse()
{
    m_morseChunks.clear();
    if (m_morseTimer) m_morseTimer->stop();
    if (m_morseActive) {
        m_morseActive = false;
        emit morseBusy(false);
    }
}

void Ft891Controller::stopMorse()
{
    const bool wasActive = m_morseActive;
    finishMorse();
    if (!m_link->isOpen()) return;
    // A playing memory stops when the keyer is switched off; it is switched
    // back on at once if it was on. Hamlib stops it the same way.
    const bool keyerOn = m_values.value(QStringLiteral("KEYER"), QStringLiteral("1")) != QLatin1String("0");
    m_link->writeNow("KR0;");
    if (keyerOn) {
        // Pending until sent: if the link closes first — the server stopped
        // during a message — close() sends it, or the radio would be left
        // with its keyer off.
        m_keyerRestorePending = true;
        QTimer::singleShot(80, this, [this] {
            if (!m_keyerRestorePending) return;
            m_keyerRestorePending = false;
            if (m_link->isOpen()) m_link->writeNow("KR1;");
        });
    }
    if (wasActive) emit logMessage(tr("Keying stopped"));
}

} // namespace rr
