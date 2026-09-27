// FT891Remote — field test on a real FT-891.
//
// Everything that has not been tried on a radio yet, in one guided run:
// what CAT can measure is checked automatically, what only the radio's
// display or sound can show is asked of the operator, and the checks that
// transmit only run with --tx. A snapshot of the radio is taken first and
// restored at the end — also after an error or Ctrl+C — then read again and
// compared, so that the radio is left as it was found. The snapshot is saved
// to a file: --restore FILE replays it if the program was stopped hard.
//
//   ft891-fieldtest /dev/ttyUSB0 38400              no transmission
//   ft891-fieldtest COM5 38400 --tx                 with transmission (dummy load!)
//   ft891-fieldtest /dev/ttyUSB0 38400 --power      also switch the radio off and on
//   ft891-fieldtest /dev/ttyUSB0 38400 --checklist  then the checks outside CAT
//   ft891-fieldtest /dev/ttyUSB0 38400 --restore ft891-snapshot-….json
//
// The server must be stopped: the test opens the CAT port itself.
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <functional>

#include "../common/catcommands.h"
#include "../common/ft891.h"
#include "../common/protocol.h"
#include "../server/ft891controller.h"

#ifndef RR_VERSION
#define RR_VERSION "0.0.0"
#endif

using namespace rr;

namespace {

std::atomic<bool> g_abort{false};
extern "C" void onInterrupt(int) { g_abort.store(true); }

QTextStream out(stdout);

// The groups whose settings the radio may keep per mode: read, and put back,
// in every mode the test visits.
const QStringList kPerModeGroups = {
    QStringLiteral("FUNCTION-1"), QStringLiteral("FUNCTION-2"), QStringLiteral("CW SETTING"),
    QStringLiteral("FM SETTING"), QStringLiteral("FRONT PANEL")};
// Every mode the checks switch to.
const QStringList kVisitedModes = {
    QStringLiteral("LSB"), QStringLiteral("USB"), QStringLiteral("CW-U"), QStringLiteral("CW-L"),
    QStringLiteral("RTTY-L"), QStringLiteral("DATA-U"), QStringLiteral("AM"), QStringLiteral("FM")};
// Never written back: they transmit, switch the radio, reset it, change the
// link's speed, or are answers only.
const QStringList kNeverRestore = {
    QStringLiteral("MOX"), QStringLiteral("TX"), QStringLiteral("PS"), QStringLiteral("AI"),
    QStringLiteral("EX1701"), QStringLiteral("EX0506"), QStringLiteral("MC")};

struct Result {
    QString section;
    QString name;
    QString status;   // PASS, FAIL, SKIP, INFO
    QString detail;
};

class FieldTest {
public:
    QString port;
    int baud = 38400;
    bool interactive = true;
    bool tx = false;
    bool power = false;
    bool checklist = false;
    QString memChannel;      // --mem
    QString restoreFile;     // --restore

    int run();

private:
    // --------------------------------------------------------- plumbing
    Ft891Controller radio;
    Ft891State st;
    QHash<QString, QString> vals;
    QStringList notices;
    QString lastReply;
    bool replied = false;
    int progressTotal = 0;
    bool progressFinished = false;
    bool morseBusy = false;

    QList<Result> results;
    QString section;
    QString stamp;
    QString reportPath;
    QString snapshotPath;
    QJsonObject snap;
    QStringList followUps;   // things the operator must do by hand afterwards

    bool aborted() const { return g_abort.load(); }
    bool waitFor(const std::function<bool()> &cond, int ms);
    void pause(int ms) { waitFor([] { return false; }, ms); }
    void begin(const QString &name);
    void record(const QString &status, const QString &name, const QString &detail = QString());
    void check(bool ok, const QString &name, const QString &detail = QString());
    void info(const QString &name, const QString &detail) { record(QStringLiteral("INFO"), name, detail); }
    QChar ask(const QString &question);
    void askCheck(const QString &question, const QString &name);
    void writeReport();

    void set(const QString &code, const QStringList &v);
    void set1(const QString &code, const QString &v) { set(code, {v}); }
    bool waitVal(const QString &code, const QString &want, int ms = 2500);
    QString raw(const QString &frame, int ms = 1500);        // sent, answer awaited
    bool rawRefused(const QString &frame, int ms = 1200);    // a set: refused?
    bool readAllAndWait(int ms);
    bool readGroupsAndWait(const QStringList &groups, int ms = 30000);
    bool setMode(const QString &mode);
    bool setFreqA(quint64 hz);
    bool setFreqB(quint64 hz);
    bool toVfo();
    QString currentBs() const;

    // ------------------------------------------------------ snapshot
    bool takeSnapshot();
    void saveSnapshot();
    bool loadSnapshot(const QString &file);
    void restoreSnapshot();
    void restoreCode(const QString &code, const QString &raw);
    int restoreGroups(const QStringList &groups, const QJsonObject &from);
    void verifyRestore();

    // --------------------------------------------------------- checks
    void checkLink();
    void checkFrequencyAndModes();
    void checkBands();
    void checkSplit();
    void checkWidthAndShift();
    void checkAgc();
    void checkRfGain();
    void checkClarifier();
    void checkCw();
    void checkMemories();
    void checkMisc();
    void checkPower();
    void checkTransmit();
    void runChecklist();
};

// ==================================================================== plumbing
bool FieldTest::waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms && !aborted())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

void FieldTest::begin(const QString &name)
{
    section = name;
    out << Qt::endl << "== " << name << Qt::endl;
}

void FieldTest::record(const QString &status, const QString &name, const QString &detail)
{
    results.append({section, name, status, detail});
    out << "  " << status.leftJustified(5) << " " << name;
    if (!detail.isEmpty()) out << " — " << detail;
    out << Qt::endl;
    writeReport();   // kept on disk as the test goes: nothing is lost on a crash
}

void FieldTest::check(bool ok, const QString &name, const QString &detail)
{
    // A wait cut short by Ctrl+C is not a failure of the radio.
    if (!ok && aborted()) {
        record(QStringLiteral("SKIP"), name, QStringLiteral("interrupted"));
        return;
    }
    record(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), name, detail);
}

QChar FieldTest::ask(const QString &question)
{
    if (!interactive || aborted()) return QLatin1Char('s');
    out << "  ?     " << question << " [y/n/s] " << Qt::flush;
    // Blocking read: the radio's poll waits meanwhile, which is harmless.
    char buf[256] = {0};
    if (!std::fgets(buf, sizeof buf, stdin)) return QLatin1Char('s');
    const QString a = QString::fromLocal8Bit(buf).trimmed().toLower();
    if (a.startsWith(QLatin1Char('y')) || a.startsWith(QLatin1Char('o'))) return QLatin1Char('y');
    if (a.startsWith(QLatin1Char('n'))) return QLatin1Char('n');
    return QLatin1Char('s');
}

void FieldTest::askCheck(const QString &question, const QString &name)
{
    const QChar a = ask(question);
    if (a == QLatin1Char('y')) record(QStringLiteral("PASS"), name, QStringLiteral("seen by the operator"));
    else if (a == QLatin1Char('n')) record(QStringLiteral("FAIL"), name, QStringLiteral("not seen by the operator"));
    else record(QStringLiteral("SKIP"), name, QStringLiteral("not checked on the radio"));
}

void FieldTest::writeReport()
{
    QFile f(reportPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream r(&f);
    r.setEncoding(QStringConverter::Utf8);
    int pass = 0, fail = 0, skip = 0;
    for (const Result &x : results) {
        if (x.status == QLatin1String("PASS")) ++pass;
        else if (x.status == QLatin1String("FAIL")) ++fail;
        else if (x.status == QLatin1String("SKIP")) ++skip;
    }
    r << "# FT891Remote field test\n\n"
      << "- Program: ft891-fieldtest " << RR_VERSION << "\n"
      << "- Date: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
      << "- Port: " << port << " at " << baud << " baud\n"
      << "- Transmission checks: " << (tx ? "yes" : "no") << "; power switch: " << (power ? "yes" : "no") << "\n"
      << "- Snapshot: " << snapshotPath << "\n\n"
      << "**" << pass << " passed, " << fail << " failed, " << skip << " not checked**\n";
    QString current;
    for (const Result &x : results) {
        if (x.section != current) {
            current = x.section;
            r << "\n## " << current << "\n\n| Result | Check | Detail |\n|---|---|---|\n";
        }
        QString d = x.detail;
        d.replace(QLatin1Char('|'), QStringLiteral("\\|"));
        r << "| " << x.status << " | " << x.name << " | " << d << " |\n";
    }
    if (!followUps.isEmpty()) {
        r << "\n## To do by hand\n\n";
        for (const QString &s : followUps) r << "- " << s << "\n";
    }
}

void FieldTest::set(const QString &code, const QStringList &v)
{
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), code}, {QStringLiteral("v"), v}});
}

bool FieldTest::waitVal(const QString &code, const QString &want, int ms)
{
    return waitFor([&] { return vals.value(code) == want; }, ms);
}

QString FieldTest::raw(const QString &frame, int ms)
{
    replied = false;
    lastReply.clear();
    radio.sendCatString(frame, true);
    waitFor([&] { return replied; }, ms);
    return replied ? lastReply : QString();
}

bool FieldTest::rawRefused(const QString &frame, int ms)
{
    notices.clear();
    radio.sendCatString(frame, false);
    return waitFor([&] { return !notices.isEmpty(); }, ms);
}

bool FieldTest::readAllAndWait(int ms)
{
    progressTotal = 0;
    progressFinished = false;
    radio.command(QStringLiteral("read"), {{QStringLiteral("all"), true}});
    const bool ok = waitFor([&] { return progressFinished; }, ms);
    pause(300);   // the last answers reach the map
    return ok;
}

bool FieldTest::readGroupsAndWait(const QStringList &groups, int ms)
{
    progressTotal = 0;
    progressFinished = false;
    for (const QString &g : groups)
        radio.command(QStringLiteral("read"), {{QStringLiteral("group"), g}});
    const bool ok = waitFor([&] { return progressFinished; }, ms);
    pause(250);
    return ok;
}

bool FieldTest::setMode(const QString &mode)
{
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), mode}});
    return waitFor([&] { return st.mode == mode; }, 3000);
}

bool FieldTest::setFreqA(quint64 hz)
{
    radio.command(QStringLiteral("freq"), {{QStringLiteral("hz"), double(hz)}, {QStringLiteral("vfo"), QStringLiteral("A")}});
    return waitFor([&] { return st.freqA == hz; }, 3000);
}

bool FieldTest::setFreqB(quint64 hz)
{
    radio.command(QStringLiteral("freq"), {{QStringLiteral("hz"), double(hz)}, {QStringLiteral("vfo"), QStringLiteral("B")}});
    return waitFor([&] { return st.freqB == hz; }, 3000);
}

bool FieldTest::toVfo()
{
    if (st.memMode == QLatin1String("VFO")) return true;
    set(QStringLiteral("VM"), {});
    return waitFor([&] { return st.memMode == QLatin1String("VFO"); }, 3000);
}

QString FieldTest::currentBs() const
{
    const int i = ft891::bandIndexFor(st.freqA);
    if (i < 0) return QStringLiteral("11");   // outside the amateur bands: GEN
    return ft891::bands().at(i).bs;          // empty for 60 m
}

// ==================================================================== snapshot
bool FieldTest::takeSnapshot()
{
    begin(QStringLiteral("Snapshot of the radio"));
    // Everything Read all reads: settings, menus, memories — and the time it
    // takes, one of the points to check.
    QElapsedTimer clock;
    clock.start();
    const bool all = readAllAndWait(180000);
    const qint64 ms = clock.elapsed();
    radio.command(QStringLiteral("read"), {{QStringLiteral("codes"), QStringList{QStringLiteral("FS"), QStringLiteral("MC")}}});
    pause(800);
    check(all, QStringLiteral("Read all"), QStringLiteral("%1 reads in %2 s").arg(progressTotal).arg(ms / 1000.0, 0, 'f', 1));

    // The state, noted once Read all is done: VFO-B is only read every few
    // poll cycles, and was still unknown when the radio first answered.
    waitFor([&] { return st.freqB > 0; }, 3000);
    const bool wasMem = st.memMode != QLatin1String("VFO");
    QJsonObject state{{QStringLiteral("freqA"), double(st.freqA)}, {QStringLiteral("freqB"), double(st.freqB)},
                      {QStringLiteral("mode"), st.mode}, {QStringLiteral("memMode"), st.memMode},
                      {QStringLiteral("memChannel"), st.memChannel}, {QStringLiteral("memName"), st.memName},
                      {QStringLiteral("clarOffset"), st.clarOffset}};

    QJsonObject settings, memories;
    QStringList missing;
    for (const CatGroup &g : ft891::description().groups) {
        if (g.poll == QLatin1String("core") || g.poll == QLatin1String("meter")) continue;
        for (const CatCommand &c : g.commands) {
            if (!c.canRead() || (c.params.isEmpty() && c.canSet())) continue;
            if (vals.contains(c.code)) settings.insert(c.code, vals.value(c.code));
            else missing << c.code;
        }
    }
    for (const QString &k : {QStringLiteral("FS"), QStringLiteral("MC")})
        if (vals.contains(k)) settings.insert(k, vals.value(k));
    for (const QString &ch : ft891::memoryChannels()) {
        const QString key = ft891::memoryKey(ch);
        if (vals.contains(key)) memories.insert(ch, vals.value(key));
        else missing << key;
    }
    // Refused outside FM: not a fault.
    if (ft891::modeFamily(st.mode) != QLatin1String("FM")) missing.removeAll(QStringLiteral("RPT"));
    check(missing.isEmpty(), QStringLiteral("every setting and memory read"),
          missing.isEmpty() ? QStringLiteral("%1 settings, %2 memories").arg(settings.size()).arg(memories.size())
                            : QStringLiteral("no value for: %1").arg(missing.join(QLatin1Char(' '))));

    // The checks run on the VFO.
    if (wasMem && !toVfo()) {
        record(QStringLiteral("FAIL"), QStringLiteral("switch to VFO"), QStringLiteral("V/M did not leave memory mode"));
        return false;
    }
    state.insert(QStringLiteral("vfoFreqA"), double(st.freqA));
    state.insert(QStringLiteral("vfoMode"), st.mode);
    state.insert(QStringLiteral("split"), vals.value(QStringLiteral("SPL")));
    const QString origBs = currentBs();
    state.insert(QStringLiteral("bs"), origBs);

    // Settings the radio may keep per mode, in every mode the test visits.
    QJsonObject perMode;
    const QString origMode = st.mode;
    for (const QString &m : kVisitedModes) {
        if (aborted()) return false;
        if (!setMode(m)) { info(QStringLiteral("mode %1").arg(m), QStringLiteral("could not be selected for the snapshot")); continue; }
        readGroupsAndWait(kPerModeGroups);
        QJsonObject o;
        for (const CatGroup &g : ft891::description().groups)
            if (kPerModeGroups.contains(g.name))
                for (const CatCommand &c : g.commands)
                    if (c.canRead() && !c.params.isEmpty() && vals.contains(c.code)) o.insert(c.code, vals.value(c.code));
        perMode.insert(m, o);
    }
    // Back to the original mode before leaving the band: leaving it stores
    // the band's stack.
    setMode(origMode);
    setFreqA(quint64(state.value(QStringLiteral("vfoFreqA")).toDouble()));
    readGroupsAndWait(kPerModeGroups);

    // The band stacks: each band is selected and what it returns to noted.
    QJsonObject bands;
    for (const ft891::BandInfo &b : ft891::bands()) {
        if (aborted()) return false;
        if (b.bs.isEmpty()) continue;
        if (rawRefused(QStringLiteral("BS%1;").arg(b.bs), 800)) continue;
        pause(500);
        raw(QStringLiteral("IF;"));
        waitFor([&] { return st.freqA > 0; }, 1000);
        bands.insert(b.bs, QJsonObject{{QStringLiteral("hz"), double(st.freqA)}, {QStringLiteral("mode"), st.mode}});
    }
    // Back to where the radio was.
    if (!origBs.isEmpty()) { rawRefused(QStringLiteral("BS%1;").arg(origBs), 800); pause(500); }
    setMode(origMode);
    setFreqA(quint64(state.value(QStringLiteral("vfoFreqA")).toDouble()));
    setFreqB(quint64(state.value(QStringLiteral("freqB")).toDouble()));
    info(QStringLiteral("band stacks"), QStringLiteral("%1 bands noted").arg(bands.size()));

    snap = QJsonObject{{QStringLiteral("program"), QStringLiteral("ft891-fieldtest")},
                       {QStringLiteral("version"), QStringLiteral(RR_VERSION)},
                       {QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                       {QStringLiteral("state"), state}, {QStringLiteral("settings"), settings},
                       {QStringLiteral("perMode"), perMode}, {QStringLiteral("bands"), bands},
                       {QStringLiteral("memories"), memories}};
    saveSnapshot();
    info(QStringLiteral("snapshot saved"), snapshotPath);
    return true;
}

void FieldTest::saveSnapshot()
{
    QFile f(snapshotPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(snap).toJson(QJsonDocument::Indented));
}

bool FieldTest::loadSnapshot(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    snap = QJsonDocument::fromJson(f.readAll()).object();
    return snap.value(QStringLiteral("program")).toString() == QLatin1String("ft891-fieldtest");
}

// A setting put back as the snapshot has it. An answer-only value (AGC
// AUTO-SLOW) is written as the value it stands for (AUTO).
void FieldTest::restoreCode(const QString &code, const QString &rawValue)
{
    if (kNeverRestore.contains(code)) return;
    const CatCommand *c = ft891::command(code);
    if (!c || !c->canSet() || c->params.isEmpty()) return;
    const bool text = c->params.size() == 1 && c->params.at(0).type == CatParam::Text;
    if (text && (rawValue.trimmed().isEmpty() || rawValue.trimmed() == QLatin1String("}"))) {
        // "KM2;" would read the memory. The radio answers "}" for an empty
        // one — its end-of-text mark — so that is what is written back.
        QString frame = c->setTemplate;
        frame.replace(QStringLiteral("{p1}"), QStringLiteral("}"));
        radio.sendCatString(frame, false);
        return;
    }
    QStringList v = text ? QStringList{rawValue.trimmed()} : c->splitAnswer(rawValue);
    for (int i = 0; i < c->params.size() && i < v.size(); ++i)
        for (const CatParamValue &pv : c->params.at(i).values)
            if (pv.readOnly && pv.value == v.at(i)) v[i] = pv.as;
    set(code, v);
}

// The settings of these groups that differ from the snapshot, NARROW first
// and WIDTH last: the steps WIDTH takes depend on NARROW.
int FieldTest::restoreGroups(const QStringList &groups, const QJsonObject &from)
{
    QStringList codes;
    for (const CatGroup &g : ft891::description().groups)
        if (groups.contains(g.name))
            for (const CatCommand &c : g.commands)
                if (from.contains(c.code)) codes << c.code;
    std::stable_sort(codes.begin(), codes.end(), [](const QString &a, const QString &b) {
        auto rank = [](const QString &x) { return x == QLatin1String("NAR") ? 0 : x == QLatin1String("WDH") ? 2 : 1; };
        return rank(a) < rank(b);
    });
    int n = 0;
    for (const QString &code : codes) {
        const QString want = from.value(code).toString();
        if (vals.value(code) == want || (code.startsWith(QLatin1String("KM")) && vals.value(code).trimmed() == want.trimmed()))
            continue;
        restoreCode(code, want);
        ++n;
        pause(60);
    }
    pause(600);
    return n;
}

void FieldTest::restoreSnapshot()
{
    const bool wasAborted = aborted();
    g_abort.store(false);    // the restore itself must run to its end
    begin(QStringLiteral("Restore"));
    if (wasAborted) info(QStringLiteral("interrupted"), QStringLiteral("the checks were stopped; restoring now"));

    const QJsonObject state = snap.value(QStringLiteral("state")).toObject();
    const QJsonObject settings = snap.value(QStringLiteral("settings")).toObject();
    const QJsonObject perMode = snap.value(QStringLiteral("perMode")).toObject();
    const QJsonObject bands = snap.value(QStringLiteral("bands")).toObject();
    const QJsonObject memories = snap.value(QStringLiteral("memories")).toObject();

    // Anything still running first.
    radio.setPtt(false);
    radio.stopMorse();
    if (vals.value(QStringLiteral("SCAN")) != QLatin1String("0")) set1(QStringLiteral("SCAN"), QStringLiteral("0"));
    pause(300);
    toVfo();

    // The band stacks: each band gets back its frequency and mode; leaving
    // it stores them.
    const QString origBs = state.value(QStringLiteral("bs")).toString();
    int nb = 0;
    for (auto it = bands.constBegin(); it != bands.constEnd(); ++it) {
        if (it.key() == origBs) continue;
        if (rawRefused(QStringLiteral("BS%1;").arg(it.key()), 800)) continue;
        pause(500);
        const QJsonObject b = it.value().toObject();
        setMode(b.value(QStringLiteral("mode")).toString());
        setFreqA(quint64(b.value(QStringLiteral("hz")).toDouble()));
        ++nb;
    }
    info(QStringLiteral("band stacks"), QStringLiteral("%1 bands put back").arg(nb));

    // The settings kept per mode, mode by mode.
    const QString origMode = state.value(QStringLiteral("vfoMode")).toString();
    int nm = 0;
    for (auto it = perMode.constBegin(); it != perMode.constEnd(); ++it) {
        if (it.key() == origMode) continue;
        if (!setMode(it.key())) continue;
        readGroupsAndWait(kPerModeGroups);
        nm += restoreGroups(kPerModeGroups, it.value().toObject());
    }
    info(QStringLiteral("settings per mode"), QStringLiteral("%1 written back").arg(nm));

    // The original band, VFOs and mode, then its settings.
    if (!origBs.isEmpty()) { rawRefused(QStringLiteral("BS%1;").arg(origBs), 800); pause(500); }
    setMode(origMode);
    setFreqA(quint64(state.value(QStringLiteral("vfoFreqA")).toDouble()));
    if (state.value(QStringLiteral("freqB")).toDouble() > 0)
        setFreqB(quint64(state.value(QStringLiteral("freqB")).toDouble()));
    readGroupsAndWait(kPerModeGroups);
    int ns = restoreGroups(kPerModeGroups, perMode.contains(origMode) ? perMode.value(origMode).toObject() : settings);

    // Everything else: menus, keyer memories, and whatever is left.
    QStringList others;
    for (const CatGroup &g : ft891::description().groups)
        if (!kPerModeGroups.contains(g.name) && g.poll != QLatin1String("meter")) others << g.name;
    // Read again first: a keyer memory written by the CW check (KM1, through
    // the keyer path) is not read back, and would look unchanged.
    readGroupsAndWait(others, 120000);
    ns += restoreGroups(others, settings);
    info(QStringLiteral("settings and menus"), QStringLiteral("%1 written back").arg(ns));

    // Clarifier offset (relative steps), then split.
    const int dClar = state.value(QStringLiteral("clarOffset")).toInt() - st.clarOffset;
    if (dClar != 0) {
        radio.command(QStringLiteral("clar"), {{QStringLiteral("delta"), dClar}});
        pause(600);
    }
    if (settings.contains(QStringLiteral("SPL"))) restoreCode(QStringLiteral("SPL"), settings.value(QStringLiteral("SPL")).toString());
    if (settings.contains(QStringLiteral("CLAR"))) restoreCode(QStringLiteral("CLAR"), settings.value(QStringLiteral("CLAR")).toString());
    if (settings.contains(QStringLiteral("FS"))) restoreCode(QStringLiteral("FS"), settings.value(QStringLiteral("FS")).toString());

    // The memory channel the test wrote.
    const QString ch = snap.value(QStringLiteral("memoryTestChannel")).toString();
    if (!ch.isEmpty()) {
        const QString before = memories.value(ch).toString();
        if (before.isEmpty()) {
            followUps << QStringLiteral("Memory channel %1 was empty and now holds the test's data: "
                                        "CAT has no command to clear it — clear it from the radio.").arg(ch);
            info(QStringLiteral("memory %1").arg(ch), QStringLiteral("was empty: cannot be cleared over CAT"));
        } else {
            const ft891::IfInfo m = ft891::parseIf(QStringLiteral("IF") + before);
            const ft891::ModeInfo *mi = ft891::modeByCode(m.mode);
            radio.command(QStringLiteral("memwrite"),
                          {{QStringLiteral("ch"), ch}, {QStringLiteral("hz"), double(m.freq)},
                           {QStringLiteral("mode"), mi ? mi->name : QStringLiteral("USB")},
                           {QStringLiteral("clarOn"), m.clarOn}, {QStringLiteral("clar"), m.clarOffset},
                           {QStringLiteral("tone"), m.tone}, {QStringLiteral("shift"), m.shift}});
            pause(800);
            info(QStringLiteral("memory %1").arg(ch), QStringLiteral("written back"));
        }
    }

    // Memory mode, if the radio was in it.
    if (state.value(QStringLiteral("memMode")).toString() != QLatin1String("VFO")) {
        // By name: a PMS channel (P1L…) has no number.
        QString name = state.value(QStringLiteral("memName")).toString();
        if (!ft891::isMemoryChannel(name))
            name = QStringLiteral("%1").arg(state.value(QStringLiteral("memChannel")).toInt(), 3, 10, QLatin1Char('0'));
        radio.command(QStringLiteral("memrecall"), {{QStringLiteral("ch"), name}});
        waitFor([&] { return st.memMode != QLatin1String("VFO"); }, 3000);
    }
    verifyRestore();
}

void FieldTest::verifyRestore()
{
    begin(QStringLiteral("Restore — verification"));
    readAllAndWait(180000);
    const QJsonObject state = snap.value(QStringLiteral("state")).toObject();
    const QJsonObject settings = snap.value(QStringLiteral("settings")).toObject();
    const QJsonObject perMode = snap.value(QStringLiteral("perMode")).toObject();
    const QJsonObject memories = snap.value(QStringLiteral("memories")).toObject();
    const QString origMode = state.value(QStringLiteral("vfoMode")).toString();
    // The per-mode snapshot of the original mode is the fresher one.
    const QJsonObject modeNow = perMode.value(origMode).toObject();

    QStringList diffs;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const QString code = it.key();
        if (kNeverRestore.contains(code) || code == QLatin1String("RPT")) continue;
        const QString want = modeNow.contains(code) ? modeNow.value(code).toString() : it.value().toString();
        const QString now = vals.value(code);
        if (now == want || (code.startsWith(QLatin1String("KM")) && now.trimmed() == want.trimmed())) continue;
        // An answer-only value may come back as another of its family:
        // AUTO answers FAST, MID or SLOW depending on the mode.
        const CatCommand *c = ft891::command(code);
        bool sameFamily = false;
        if (c && c->params.size() == 1)
            for (const CatParamValue &pv : c->params.at(0).values)
                if ((pv.readOnly && pv.value == want && pv.as == now) || (pv.readOnly && pv.value == now && pv.as == want))
                    sameFamily = true;
        if (!sameFamily) diffs << QStringLiteral("%1: %2 → %3").arg(code, want, now);
    }
    for (auto it = memories.constBegin(); it != memories.constEnd(); ++it) {
        if (it.key() == snap.value(QStringLiteral("memoryTestChannel")).toString() && it.value().toString().isEmpty())
            continue;   // reported: cannot be cleared
        if (vals.value(ft891::memoryKey(it.key())) != it.value().toString())
            diffs << QStringLiteral("memory %1").arg(it.key());
    }
    check(diffs.isEmpty(), QStringLiteral("settings, menus and memories as before"),
          diffs.isEmpty() ? QStringLiteral("no difference") : diffs.join(QStringLiteral("; ")));

    const bool wasMem = state.value(QStringLiteral("memMode")).toString() != QLatin1String("VFO");
    if (wasMem) {
        const QString want = state.value(QStringLiteral("memName")).toString();
        check(st.memMode != QLatin1String("VFO")
                  && (want.isEmpty() ? st.memChannel == state.value(QStringLiteral("memChannel")).toInt() : st.memName == want),
              QStringLiteral("memory mode and channel"), QStringLiteral("%1 %2").arg(st.memMode, st.memName));
    } else {
        check(st.freqA == quint64(state.value(QStringLiteral("freqA")).toDouble()) && st.mode == origMode,
              QStringLiteral("VFO-A frequency and mode"), QStringLiteral("%1 Hz %2").arg(st.freqA).arg(st.mode));
    }
    if (state.value(QStringLiteral("freqB")).toDouble() > 0) {
        waitFor([&] { return st.freqB == quint64(state.value(QStringLiteral("freqB")).toDouble()); }, 2500);
        check(st.freqB == quint64(state.value(QStringLiteral("freqB")).toDouble()),
              QStringLiteral("VFO-B frequency"), QStringLiteral("%1 Hz").arg(st.freqB));
    }
    check(st.clarOffset == state.value(QStringLiteral("clarOffset")).toInt(),
          QStringLiteral("clarifier offset"), QStringLiteral("%1 Hz").arg(st.clarOffset));
    followUps << QStringLiteral("The band stacks were put back to the frequency and mode each band returned to; "
                                "if your FT-891 keeps several registers per band, only the first was noted.");
}

// ====================================================================== checks
void FieldTest::checkLink()
{
    begin(QStringLiteral("Link and identity"));
    check(st.radioId == QLatin1String("0650"), QStringLiteral("ID; answers 0650"), st.radioId);
    for (const QString &code : {QStringLiteral("EX1801"), QStringLiteral("EX1802"), QStringLiteral("EX1803")}) {
        const CatCommand *c = ft891::command(code);
        const QString shown = c ? ft891::displayValue(*c, vals.value(code)) : QString();
        check(shown.startsWith(QLatin1Char('V')) && shown.size() == 6,
              QStringLiteral("%1 shown as the reference writes it").arg(c ? c->name : code),
              QStringLiteral("%1 → %2").arg(vals.value(code), shown));
    }
    askCheck(QStringLiteral("Open menu 18-01 MAIN VERSION on the radio: does it show %1?")
                 .arg(ft891::displayValue(*ft891::command(QStringLiteral("EX1801")), vals.value(QStringLiteral("EX1801")))),
             QStringLiteral("version as the radio shows it"));
}

void FieldTest::checkFrequencyAndModes()
{
    if (aborted()) return;
    begin(QStringLiteral("Frequency and modes (FA, FB, MD)"));
    const quint64 a = st.freqA;
    check(setFreqA(a + 1000), QStringLiteral("FA: VFO-A 1 kHz up"), QStringLiteral("%1 Hz").arg(st.freqA));
    check(setFreqB(a + 10000), QStringLiteral("FB: VFO-B"), QStringLiteral("%1 Hz").arg(st.freqB));
    setFreqA(a);

    // From another family: SSB chosen while already in SSB keeps the
    // radio's sideband, by design.
    setMode(QStringLiteral("CW-U"));
    setFreqA(7074000);
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("SSB")}});
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("LSB"); }, 3000); check(ok_, QStringLiteral("SSB below 10 MHz gives LSB"), st.mode); }
    setFreqA(14074000);
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("CW")}});
    { const bool ok_ = waitFor([&] { return ft891::modeFamily(st.mode) == QLatin1String("CW"); }, 3000); check(ok_, QStringLiteral("CW family"), st.mode); }
    setMode(QStringLiteral("CW-L"));
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("SSB")}});
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("USB"); }, 3000); check(ok_, QStringLiteral("SSB from 10 MHz up gives USB"), st.mode); }
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("CW")}});
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("CW-L"); }, 3000); check(ok_, QStringLiteral("back to CW: CW-L, last used"), st.mode); }
    for (const QString &f : {QStringLiteral("RTTY"), QStringLiteral("DATA"), QStringLiteral("AM"), QStringLiteral("FM")}) {
        radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), f}});
        { const bool ok_ = waitFor([&] { return ft891::modeFamily(st.mode) == f; }, 3000); check(ok_, QStringLiteral("%1 family").arg(f), st.mode); }
    }
    askCheck(QStringLiteral("Does the radio show FM now, on %1 MHz?").arg(st.freqA / 1e6, 0, 'f', 3),
             QStringLiteral("mode as the radio shows it"));
    setMode(QStringLiteral("USB"));
}

void FieldTest::checkBands()
{
    if (aborted()) return;
    begin(QStringLiteral("Band keys (BS, BU, BD)"));
    for (const ft891::BandInfo &b : ft891::bands()) {
        if (aborted()) return;
        if (b.bs.isEmpty()) continue;
        const bool refused = rawRefused(QStringLiteral("BS%1;").arg(b.bs), 800);
        pause(400);
        raw(QStringLiteral("IF;"));
        const bool inside = b.name == QLatin1String("GEN") || (st.freqA >= b.low && st.freqA <= b.high);
        check(!refused && inside, QStringLiteral("BS%1 selects %2").arg(b.bs, b.name == QLatin1String("GEN") || b.name == QLatin1String("MW") ? b.name : b.name + QStringLiteral(" m")),
              refused ? QStringLiteral("refused") : QStringLiteral("%1 Hz").arg(st.freqA));
    }
    check(rawRefused(QStringLiteral("BS02;"), 1000), QStringLiteral("BS02 is not used"), QStringLiteral("refused as the reference says"));

    int i60 = -1, i20 = -1;
    for (int i = 0; i < ft891::bands().size(); ++i) {
        if (ft891::bands().at(i).name == QLatin1String("60")) i60 = i;
        if (ft891::bands().at(i).name == QLatin1String("20")) i20 = i;
    }
    radio.command(QStringLiteral("band"), {{QStringLiteral("index"), i60}});
    { const bool ok_ = waitFor([&] { return st.freqA == 5354000; }, 3000); check(ok_, QStringLiteral("60 m: its preset frequency"), QStringLiteral("%1 Hz").arg(st.freqA)); }

    rawRefused(QStringLiteral("BS05;"), 800);
    pause(500);
    set(QStringLiteral("BU"), {});
    pause(900);
    raw(QStringLiteral("IF;"));
    const int up = ft891::bandIndexFor(st.freqA);
    check(up >= 0 && up != i20, QStringLiteral("BU: band up from 20 m"),
          up >= 0 ? ft891::bands().at(up).name + QStringLiteral(" m") : QStringLiteral("%1 Hz").arg(st.freqA));
    set(QStringLiteral("BD"), {});
    pause(900);
    raw(QStringLiteral("IF;"));
    check(ft891::bandIndexFor(st.freqA) == i20, QStringLiteral("BD: band down, back to 20 m"), QStringLiteral("%1 Hz").arg(st.freqA));
    setFreqA(14074000);
    setMode(QStringLiteral("USB"));
}

void FieldTest::checkSplit()
{
    if (aborted()) return;
    begin(QStringLiteral("SPLIT and quick split (ST, RI, QS)"));
    setFreqB(st.freqA + 3000);
    set1(QStringLiteral("SPL"), QStringLiteral("1"));
    { const bool ok_ = waitVal(QStringLiteral("SPL"), QStringLiteral("1")); check(ok_, QStringLiteral("SPLIT on with ST1, read with RIC"), vals.value(QStringLiteral("SPL"))); }
    askCheck(QStringLiteral("Is SPLIT shown on the radio?"), QStringLiteral("SPLIT on, on the radio"));
    set1(QStringLiteral("SPL"), QStringLiteral("0"));
    { const bool ok_ = waitVal(QStringLiteral("SPL"), QStringLiteral("0")); check(ok_, QStringLiteral("SPLIT off with ST0"), vals.value(QStringLiteral("SPL"))); }
    askCheck(QStringLiteral("Has SPLIT gone off on the radio?"), QStringLiteral("SPLIT off, on the radio"));

    set1(QStringLiteral("EX0513"), QStringLiteral("5"));
    waitVal(QStringLiteral("EX0513"), QStringLiteral("+05"));
    set(QStringLiteral("QSPL"), {});
    const bool qs = waitFor([&] { return vals.value(QStringLiteral("SPL")) == QLatin1String("1")
                                        && st.freqB == st.freqA + 5000; }, 3000);
    check(qs, QStringLiteral("QS: VFO-B at VFO-A + 5 kHz, split on"),
          QStringLiteral("A %1, B %2, split %3").arg(st.freqA).arg(st.freqB).arg(vals.value(QStringLiteral("SPL"))));
    askCheck(QStringLiteral("Does the radio show split on, with VFO-B 5 kHz above VFO-A?"), QStringLiteral("quick split on the radio"));
    set1(QStringLiteral("SPL"), QStringLiteral("0"));
    waitVal(QStringLiteral("SPL"), QStringLiteral("0"));
}

void FieldTest::checkWidthAndShift()
{
    if (aborted()) return;
    begin(QStringLiteral("WIDTH and IF SHIFT (SH, IS)"));
    // WIDTH on, then the step: SH0 1 14.
    auto width = [&](const QString &step) { notices.clear(); set(QStringLiteral("WDH"), {QStringLiteral("1"), step}); };
    setMode(QStringLiteral("USB"));
    set1(QStringLiteral("NAR"), QStringLiteral("0"));
    waitVal(QStringLiteral("NAR"), QStringLiteral("0"));
    width(QStringLiteral("14"));
    { const bool ok_ = waitVal(QStringLiteral("WDH"), QStringLiteral("114")); check(ok_, QStringLiteral("USB wide: WIDTH on, step 14 (2400 Hz), as SH0114"), vals.value(QStringLiteral("WDH"))); }
    askCheck(QStringLiteral("Turn nothing: does the radio's WIDTH indication show 2400 Hz?"), QStringLiteral("2400 Hz on the radio"));
    set(QStringLiteral("WDH"), {QStringLiteral("0"), QStringLiteral("14")});
    { const bool ok_ = waitVal(QStringLiteral("WDH"), QStringLiteral("014")); check(ok_, QStringLiteral("WIDTH off (SH0014): the mode's default width"), vals.value(QStringLiteral("WDH"))); }
    set1(QStringLiteral("NAR"), QStringLiteral("1"));
    waitVal(QStringLiteral("NAR"), QStringLiteral("1"));
    pause(500);
    radio.command(QStringLiteral("read"), {{QStringLiteral("codes"), QStringList{QStringLiteral("WDH")}}});
    pause(600);
    info(QStringLiteral("what NARROW on does to the step"), QStringLiteral("the radio reports step %1").arg(vals.value(QStringLiteral("WDH"))));
    width(QStringLiteral("03"));
    { const bool ok_ = waitVal(QStringLiteral("WDH"), QStringLiteral("103")); check(ok_, QStringLiteral("USB narrow: step 03 (600 Hz)"), vals.value(QStringLiteral("WDH"))); }
    set1(QStringLiteral("NAR"), QStringLiteral("0"));
    waitVal(QStringLiteral("NAR"), QStringLiteral("0"));

    setMode(QStringLiteral("CW-U"));
    width(QStringLiteral("17"));
    { const bool ok_ = waitVal(QStringLiteral("WDH"), QStringLiteral("117")); check(ok_, QStringLiteral("CW wide: step 17 (3000 Hz)"), vals.value(QStringLiteral("WDH"))); }
    width(QStringLiteral("09"));
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000); check(ok_, QStringLiteral("CW wide: step 09 refused by the server")); }
    set1(QStringLiteral("NAR"), QStringLiteral("1"));
    waitVal(QStringLiteral("NAR"), QStringLiteral("1"));
    width(QStringLiteral("05"));
    { const bool ok_ = waitVal(QStringLiteral("WDH"), QStringLiteral("105")); check(ok_, QStringLiteral("CW narrow: step 05 (250 Hz)"), vals.value(QStringLiteral("WDH"))); }
    set1(QStringLiteral("NAR"), QStringLiteral("0"));
    setMode(QStringLiteral("AM"));
    width(QStringLiteral("00"));
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000); check(ok_, QStringLiteral("AM: no WIDTH, refused by the server")); }

    setMode(QStringLiteral("USB"));
    set(QStringLiteral("SFT"), {QStringLiteral("1"), QStringLiteral("-500")});
    { const bool ok_ = waitVal(QStringLiteral("SFT"), QStringLiteral("1-0500")); check(ok_, QStringLiteral("IF SHIFT on at −500 Hz as IS01-0500"), vals.value(QStringLiteral("SFT"))); }
    set(QStringLiteral("SFT"), {QStringLiteral("1"), QStringLiteral("1000")});
    { const bool ok_ = waitVal(QStringLiteral("SFT"), QStringLiteral("1+1000")); check(ok_, QStringLiteral("IF SHIFT +1000 Hz as IS01+1000"), vals.value(QStringLiteral("SFT"))); }
    askCheck(QStringLiteral("Does the radio show the IF SHIFT moved to +1000 Hz?"), QStringLiteral("IF SHIFT on the radio"));
    set(QStringLiteral("SFT"), {QStringLiteral("0"), QStringLiteral("0")});
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("SFT")).startsWith(QLatin1Char('0')); }, 2500); check(ok_, QStringLiteral("IF SHIFT off"), vals.value(QStringLiteral("SFT"))); }
}

void FieldTest::checkAgc()
{
    if (aborted()) return;
    begin(QStringLiteral("AGC AUTO answers (GT)"));
    const CatCommand *agc = ft891::command(QStringLiteral("AGC"));
    for (const QString &m : {QStringLiteral("USB"), QStringLiteral("CW-U"), QStringLiteral("AM")}) {
        setMode(m);
        set1(QStringLiteral("AGC"), QStringLiteral("4"));
        const bool ok = waitFor([&] { const QString v = vals.value(QStringLiteral("AGC"));
                                      return v == QLatin1String("4") || v == QLatin1String("5") || v == QLatin1String("6"); }, 2500);
        check(ok, QStringLiteral("AUTO in %1").arg(m),
              QStringLiteral("answered %1, shown %2").arg(vals.value(QStringLiteral("AGC")),
                                                          agc ? ft891::displayValue(*agc, vals.value(QStringLiteral("AGC"))) : QString()));
    }
    notices.clear();
    set1(QStringLiteral("AGC"), QStringLiteral("6"));
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000); check(ok_, QStringLiteral("AUTO-SLOW refused as a setting")); }
    set1(QStringLiteral("AGC"), QStringLiteral("1"));
    { const bool ok_ = waitVal(QStringLiteral("AGC"), QStringLiteral("1")); check(ok_, QStringLiteral("FAST"), vals.value(QStringLiteral("AGC"))); }
    setMode(QStringLiteral("USB"));
}

void FieldTest::checkRfGain()
{
    if (aborted()) return;
    begin(QStringLiteral("RF GAIN range (RG)"));
    radio.sendCatString(QStringLiteral("RG0030;"), false);
    pause(300);
    check(raw(QStringLiteral("RG0;")) == QLatin1String("RG0030;"), QStringLiteral("RF GAIN 030 accepted"), lastReply);
    const bool refused = rawRefused(QStringLiteral("RG0100;"), 1000);
    pause(300);
    const QString after = raw(QStringLiteral("RG0;"));
    if (refused || after != QLatin1String("RG0100;"))
        record(QStringLiteral("PASS"), QStringLiteral("the range is 0–30, as the description says"),
               refused ? QStringLiteral("RG0100 refused") : QStringLiteral("RG0100 read back as %1").arg(after));
    else
        record(QStringLiteral("FAIL"), QStringLiteral("the range is 0–100, as the reference says: the description must change"),
               QStringLiteral("RG0100 accepted"));
    askCheck(QStringLiteral("Does the radio's RF GAIN indication show 30 or 100 now (answer y if it matches: %1)?")
                 .arg(after == QLatin1String("RG0100;") ? 100 : 30), QStringLiteral("RF GAIN on the radio"));
}

void FieldTest::checkClarifier()
{
    if (aborted()) return;
    begin(QStringLiteral("Clarifier (IF P4, menu 05-18, RU, RD, RC)"));
    set1(QStringLiteral("CLAR"), QStringLiteral("1"));
    set(QStringLiteral("CLAR-CLR"), {});
    waitFor([&] { return st.clarOffset == 0; }, 2000);
    radio.command(QStringLiteral("clar"), {{QStringLiteral("delta"), 100}});
    { const bool ok_ = waitFor([&] { return st.clarOffset == 100; }, 2500); check(ok_, QStringLiteral("RU: +100 Hz"), QStringLiteral("%1 Hz").arg(st.clarOffset)); }
    const struct { const char *sel; bool rx; bool tx; const char *name; } sides[] = {
        {"0", true, false, "RX"}, {"1", false, true, "TX"}, {"2", true, true, "TRX"}};
    for (const auto &s : sides) {
        set1(QStringLiteral("EX0518"), QString::fromLatin1(s.sel));
        waitVal(QStringLiteral("EX0518"), QString::fromLatin1(s.sel));
        { const bool ok_ = waitFor([&] { return st.rxClar == s.rx && st.txClar == s.tx; }, 3000); check(ok_,
              QStringLiteral("CLAR SELECT %1").arg(QString::fromLatin1(s.name)),
              QStringLiteral("receive %1, transmit %2").arg(st.rxClar ? "shifted" : "not shifted", st.txClar ? "shifted" : "not shifted")); }
    }
    askCheck(QStringLiteral("Does the radio show the clarifier on, +100 Hz, acting on RX and TX?"), QStringLiteral("clarifier on the radio"));
    radio.command(QStringLiteral("clar"), {{QStringLiteral("delta"), -30}});
    { const bool ok_ = waitFor([&] { return st.clarOffset == 70; }, 2500); check(ok_, QStringLiteral("RD: −30 Hz"), QStringLiteral("%1 Hz").arg(st.clarOffset)); }
    set(QStringLiteral("CLAR-CLR"), {});
    { const bool ok_ = waitFor([&] { return st.clarOffset == 0; }, 2500); check(ok_, QStringLiteral("RC: cleared"), QStringLiteral("%1 Hz").arg(st.clarOffset)); }
    set1(QStringLiteral("CLAR"), QStringLiteral("0"));
}

void FieldTest::checkCw()
{
    if (aborted()) return;
    begin(QStringLiteral("CW keyer (KM, KY6-KYA for TEXT, KY1-KY5 for MESSAGE)"));
    setMode(QStringLiteral("CW-U"));
    set1(QStringLiteral("KEYER"), QStringLiteral("1"));
    set1(QStringLiteral("BK-IN"), QStringLiteral("0"));    // sidetone only: nothing on the air
    set1(QStringLiteral("EX0408"), QStringLiteral("0"));   // memory 2: TEXT
    set1(QStringLiteral("SPEED"), QStringLiteral("20"));
    waitVal(QStringLiteral("BK-IN"), QStringLiteral("0"));
    out << "        BREAK-IN is off: the keyer plays in the sidetone only." << Qt::endl;

    set1(QStringLiteral("KM2"), QStringLiteral("fieldtest de n0call"));
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("KM2")).trimmed() == QLatin1String("FIELDTEST DE N0CALL"); }, 3000); check(ok_,
          QStringLiteral("KM2 written and read back"), vals.value(QStringLiteral("KM2")).trimmed()); }
    radio.command(QStringLiteral("cwmem"), {{QStringLiteral("n"), 2}});
    pause(4000);
    askCheck(QStringLiteral("Did you hear « FIELDTEST DE N0CALL » in the sidetone (KY7)?"), QStringLiteral("TEXT memory 2 played with KY7"));

    radio.sendMorse(QStringLiteral("TEST"));
    waitFor([&] { return !morseBusy; }, 8000);
    pause(1000);
    askCheck(QStringLiteral("Did you hear « TEST » (typed text: KM1 then KY6)?"), QStringLiteral("typed text sent"));

    if (ask(QStringLiteral("Have you recorded a paddle message in CW memory 1? (y to play it in the sidetone)")) == QLatin1Char('y')) {
        set1(QStringLiteral("EX0407"), QStringLiteral("1"));   // memory 1: MESSAGE
        waitVal(QStringLiteral("EX0407"), QStringLiteral("1"));
        radio.command(QStringLiteral("cwmem"), {{QStringLiteral("n"), 1}});   // MESSAGE: KY1
        pause(4000);
        askCheck(QStringLiteral("Did you hear your recorded message (KY1)?"), QStringLiteral("paddle message played with KY1"));
    } else {
        record(QStringLiteral("SKIP"), QStringLiteral("paddle message (KY1)"), QStringLiteral("no message recorded"));
    }

    if (tx) {
        set1(QStringLiteral("BK-IN"), QStringLiteral("1"));
        waitVal(QStringLiteral("BK-IN"), QStringLiteral("1"));
        radio.command(QStringLiteral("cwmem"), {{QStringLiteral("n"), 2}});
        { const bool ok_ = waitFor([&] { return st.ptt; }, 4000); check(ok_, QStringLiteral("with BREAK-IN, memory 2 keys the transmitter")); }
        waitFor([&] { return !st.ptt; }, 15000);
        set1(QStringLiteral("BK-IN"), QStringLiteral("0"));
    }
    setMode(QStringLiteral("USB"));
}

void FieldTest::checkMemories()
{
    if (aborted()) return;
    begin(QStringLiteral("Memories (MR, MW, MC, MA, AM, VM)"));
    const QJsonObject memories = snap.value(QStringLiteral("memories")).toObject();
    QString empty;
    for (int i = 99; i >= 1 && empty.isEmpty(); --i) {
        const QString ch = QStringLiteral("%1").arg(i, 3, 10, QLatin1Char('0'));
        if (memories.contains(ch) && memories.value(ch).toString().isEmpty()) empty = ch;
    }
    QString ch = memChannel;
    if (ch.isEmpty()) ch = empty;
    if (ch.isEmpty()) ch = QStringLiteral("099");
    const bool wasOccupied = !memories.value(ch).toString().isEmpty();
    snap.insert(QStringLiteral("memoryTestChannel"), ch);
    saveSnapshot();
    info(QStringLiteral("test channel"), wasOccupied
         ? QStringLiteral("%1, occupied: its content is written back at the end").arg(ch)
         : QStringLiteral("%1, empty: it will hold the test's data afterwards (no CAT command clears a memory)").arg(ch));

    if (!empty.isEmpty()) {
        const QString a = raw(QStringLiteral("MR%1;").arg(empty));
        info(QStringLiteral("what the radio answers for an empty channel"), a.isEmpty() ? QStringLiteral("nothing") : a);
        check(vals.contains(ft891::memoryKey(empty)) && vals.value(ft891::memoryKey(empty)).isEmpty(),
              QStringLiteral("empty channel %1 recognised").arg(empty));
    }

    toVfo();
    radio.command(QStringLiteral("memwrite"), {{QStringLiteral("ch"), ch}, {QStringLiteral("hz"), 21074000},
                                               {QStringLiteral("mode"), QStringLiteral("DATA-U")},
                                               {QStringLiteral("clarOn"), true}, {QStringLiteral("clar"), -120},
                                               {QStringLiteral("tone"), 0}, {QStringLiteral("shift"), 0}});
    const bool written = waitFor([&] {
        const ft891::IfInfo m = ft891::parseIf(QStringLiteral("IF") + vals.value(ft891::memoryKey(ch)));
        return m.ok && m.freq == 21074000 && m.mode == QLatin1Char('C') && m.clarOn && m.clarOffset == -120; }, 3000);
    check(written, QStringLiteral("MW: memory %1 written, MR reads it back").arg(ch), vals.value(ft891::memoryKey(ch)));
    if (wasOccupied)
        askCheck(QStringLiteral("Memory %1 had a name on the radio: is it still shown there (MW keeps the name)?").arg(ch),
                 QStringLiteral("name kept by MW"));

    radio.command(QStringLiteral("memrecall"), {{QStringLiteral("ch"), ch}});
    { const bool ok_ = waitFor([&] { return st.memMode != QLatin1String("VFO") && st.memChannel == ch.toInt() && st.freqA == 21074000; }, 3000); check(ok_,
          QStringLiteral("MC from VFO: memory mode on %1").arg(ch), QStringLiteral("%1 %2, %3 Hz").arg(st.memMode).arg(st.memChannel).arg(st.freqA)); }

    radio.command(QStringLiteral("memrecall"), {{QStringLiteral("ch"), ch}});
    waitFor([&] { return st.memChannel == ch.toInt(); }, 2500);
    set(QStringLiteral("MA"), {});
    pause(600);
    toVfo();
    { const bool ok_ = waitFor([&] { return st.freqA == 21074000; }, 2500); check(ok_, QStringLiteral("MA: memory to VFO-A"), QStringLiteral("%1 Hz").arg(st.freqA)); }

    setFreqA(21075000);
    // AM overwrites the selected memory: the description asks for a
    // confirmation, given here.
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("AM")},
                                          {QStringLiteral("v"), QStringList()}, {QStringLiteral("confirmed"), true}});
    pause(600);
    // Read here from the radio's own answer: the server keeps only the
    // memory answers of its own reads, not those of a raw frame.
    const QString back = raw(QStringLiteral("MR%1;").arg(ch));
    QString fields = back;
    if (fields.endsWith(QLatin1Char(';'))) fields.chop(1);
    const ft891::IfInfo m = ft891::parseIf(QStringLiteral("IF") + fields.mid(2));
    check(m.ok && m.freq == 21075000, QStringLiteral("AM: VFO-A to the selected memory (%1)").arg(ch), back);
    setFreqA(14074000);
    setMode(QStringLiteral("USB"));
}

void FieldTest::checkMisc()
{
    if (aborted()) return;
    begin(QStringLiteral("FAST, AUTO INFORMATION, SCAN (FS, AI, SC)"));
    set1(QStringLiteral("FS"), QStringLiteral("1"));
    radio.command(QStringLiteral("read"), {{QStringLiteral("codes"), QStringList{QStringLiteral("FS")}}});
    { const bool ok_ = waitVal(QStringLiteral("FS"), QStringLiteral("1")); check(ok_, QStringLiteral("FAST on"), vals.value(QStringLiteral("FS"))); }
    set1(QStringLiteral("FS"), QStringLiteral("0"));
    radio.command(QStringLiteral("read"), {{QStringLiteral("codes"), QStringList{QStringLiteral("FS")}}});
    { const bool ok_ = waitVal(QStringLiteral("FS"), QStringLiteral("0")); check(ok_, QStringLiteral("FAST off"), vals.value(QStringLiteral("FS"))); }
    set1(QStringLiteral("AI"), QStringLiteral("0"));
    radio.command(QStringLiteral("read"), {{QStringLiteral("codes"), QStringList{QStringLiteral("AI")}}});
    { const bool ok_ = waitVal(QStringLiteral("AI"), QStringLiteral("0")); check(ok_, QStringLiteral("AUTO INFORMATION off"), vals.value(QStringLiteral("AI"))); }

    set1(QStringLiteral("SCAN"), QStringLiteral("1"));
    { const bool ok_ = waitVal(QStringLiteral("SCAN"), QStringLiteral("1")); check(ok_, QStringLiteral("SCAN up started"), vals.value(QStringLiteral("SCAN"))); }
    pause(1500);
    askCheck(QStringLiteral("Is the radio scanning?"), QStringLiteral("scan seen on the radio"));
    set1(QStringLiteral("SCAN"), QStringLiteral("0"));
    { const bool ok_ = waitVal(QStringLiteral("SCAN"), QStringLiteral("0")); check(ok_, QStringLiteral("SCAN stopped"), vals.value(QStringLiteral("SCAN"))); }
    setFreqA(14074000);
}

void FieldTest::checkPower()
{
    if (aborted()) return;
    begin(QStringLiteral("Power switch (PS)"));
    if (ask(QStringLiteral("The radio will be switched off, then on again over CAT. Go ahead?")) != QLatin1Char('y')
        && interactive) {
        record(QStringLiteral("SKIP"), QStringLiteral("power off and on"), QStringLiteral("declined"));
        return;
    }
    radio.command(QStringLiteral("power"), {{QStringLiteral("on"), false}});
    { const bool ok_ = waitFor([&] { return !st.radioOn; }, 20000); check(ok_, QStringLiteral("PS0: the radio goes off")); }
    pause(3000);
    radio.command(QStringLiteral("power"), {{QStringLiteral("on"), true}});
    const bool back = waitFor([&] { return st.radioOn; }, 30000);
    check(back, QStringLiteral("PS1: the radio comes back on"));
    if (!back) {
        out << "  !     Switch the radio on by hand, then press Enter." << Qt::endl;
        char buf[16];
        if (interactive && !std::fgets(buf, sizeof buf, stdin)) buf[0] = 0;
        waitFor([&] { return st.radioOn; }, 60000);
    }
    pause(2000);
}

void FieldTest::checkTransmit()
{
    if (aborted()) return;
    begin(QStringLiteral("Transmission (dummy load)"));
    setFreqA(14074000);
    setMode(QStringLiteral("USB"));
    radio.setPtt(true);
    { const bool ok_ = waitFor([&] { return st.ptt; }, 3000); check(ok_, QStringLiteral("PTT over CAT: TX; reads 1")); }
    pause(1500);
    check(st.po > 0, QStringLiteral("power meter in transmit"), QStringLiteral("raw %1, %2 W").arg(st.po).arg(ft891::powerWatts(st.po), 0, 'f', 0));
    radio.setPtt(false);
    { const bool ok_ = waitFor([&] { return !st.ptt; }, 3000); check(ok_, QStringLiteral("PTT released")); }

    if (ask(QStringLiteral("Is an antenna tuner connected (TUNE will run a tuning cycle, which transmits)?")) == QLatin1Char('y')) {
        radio.startTune();
        const bool started = waitFor([&] { return st.ptt; }, 5000);
        const bool ended = waitFor([&] { return !st.ptt; }, 20000);
        check(started && ended, QStringLiteral("TUNE: tuning cycle ran"));
    } else {
        record(QStringLiteral("SKIP"), QStringLiteral("TUNE"), QStringLiteral("no tuner"));
    }

    if (ask(QStringLiteral("Have you recorded a voice message in DVS channel 1 (it will be transmitted)?")) == QLatin1Char('y')) {
        set1(QStringLiteral("PB"), QStringLiteral("1"));
        { const bool ok_ = waitFor([&] { return st.ptt; }, 4000); check(ok_, QStringLiteral("PB01: voice message played on the air")); }
        waitFor([&] { return !st.ptt; }, 30000);
        set1(QStringLiteral("PB"), QStringLiteral("0"));
    } else {
        record(QStringLiteral("SKIP"), QStringLiteral("DVS playback (PB)"), QStringLiteral("no message recorded"));
    }
}

// The checks CAT cannot make: asked one by one, answers in the report.
void FieldTest::runChecklist()
{
    struct Item { const char *section; const char *text; };
    static const Item items[] = {
        {"Checklist — client", "CW page: change keyer memory 2's text, press Set: the new text stays"},
        {"Checklist — client", "SPLIT key and MODE key follow what the radio shows"},
        {"Checklist — client", "MEMORY page: Read, edit a memory, Write, Recall"},
        {"Checklist — client", "Read all in SETUP: the progress bar ends, nothing left without a value"},
        {"Checklist — audio and link", "Receive and transmit audio with Opus"},
        {"Checklist — audio and link", "Receive and transmit audio with PCM"},
        {"Checklist — audio and link", "Encrypted link, and automatic reconnection after the server restarts"},
        {"Checklist — safety", "Dead man: transmitting on a dummy load, cut the client's network — TX stops within 2 s"},
        {"Checklist — safety", "Out of band: the client shows OUT OF BAND and the PTT is refused"},
        {"Checklist — rigctld", "WSJT-X on 127.0.0.1:4532: frequency, PKTUSB mode, split, PTT"},
        {"Checklist — server", "Windows: the FT-891's two ports recognised once it has answered; an SCU-17 not taken for it"},
        {"Checklist — server", "Windows 11 dark theme: every text readable"},
        {"Checklist — server", "Linux: headless server started by systemd"},
        {"Checklist — Windows", "build_all.bat /deps, then the installer on a PC without Visual C++: both programs start"},
        {"Checklist — Android", "PANEL keys all the same height; refresh and arrow icons drawn"},
        {"Checklist — Android", "SETUP fits the screen with a right margin; portrait only"},
        {"Checklist — Android", "PTT on the volume-down key; audio goes on with the screen off"},
    };
    for (const Item &it : items) {
        if (aborted()) return;
        if (section != QString::fromUtf8(it.section)) begin(QString::fromUtf8(it.section));
        const QChar a = ask(QString::fromUtf8(it.text) + QStringLiteral(" — does it work?"));
        QString comment;
        if (a == QLatin1Char('n') && interactive) {
            out << "        What happened? " << Qt::flush;
            char buf[512] = {0};
            if (std::fgets(buf, sizeof buf, stdin)) comment = QString::fromLocal8Bit(buf).trimmed();
        }
        record(a == QLatin1Char('y') ? QStringLiteral("PASS") : a == QLatin1Char('n') ? QStringLiteral("FAIL") : QStringLiteral("SKIP"),
               QString::fromUtf8(it.text), comment);
    }
}

// ========================================================================= run
int FieldTest::run()
{
    stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
    reportPath = QStringLiteral("ft891-fieldtest-%1.md").arg(stamp);
    snapshotPath = restoreFile.isEmpty() ? QStringLiteral("ft891-snapshot-%1.json").arg(stamp) : restoreFile;

    QObject::connect(&radio, &Ft891Controller::stateChanged, [&](const Ft891State &s) { st = s; });
    QObject::connect(&radio, &Ft891Controller::valuesChanged, [&](const QVariantMap &d) {
        for (auto it = d.cbegin(); it != d.cend(); ++it) vals.insert(it.key(), it.value().toString());
    });
    QObject::connect(&radio, &Ft891Controller::readProgress, [&](int, int total) {
        if (total > 0) progressTotal = qMax(progressTotal, total);
        if (total == 0 && progressTotal > 0) progressFinished = true;
    });
    QObject::connect(&radio, &Ft891Controller::notice, [&](const QString &n) { notices << n; });
    QObject::connect(&radio, &Ft891Controller::catReply, [&](const QString &r) { lastReply = r; replied = true; });
    QObject::connect(&radio, &Ft891Controller::morseBusy, [&](bool b) { morseBusy = b; });

    out << "FT891Remote field test " << RR_VERSION << " — " << port << " at " << baud << " baud" << Qt::endl;
    Ft891Config cfg;
    cfg.catPort = port;
    cfg.catBaud = baud;
    radio.open(cfg);
    begin(QStringLiteral("Connection"));
    const bool up = waitFor([&] { return st.linkOpen && st.radioOn && st.freqA > 0; }, 10000)
                    && waitFor([&] { return !st.radioId.isEmpty(); }, 3000);
    check(up, QStringLiteral("the radio answers"), up ? QStringLiteral("ID %1").arg(st.radioId) : st.error);
    if (!up) {
        out << "The radio does not answer: is the server stopped, the port and speed right (menu 05-06)?" << Qt::endl;
        return 2;
    }

    if (!restoreFile.isEmpty()) {
        if (!loadSnapshot(restoreFile)) {
            out << "Not a snapshot of this program: " << restoreFile << Qt::endl;
            return 2;
        }
        restoreSnapshot();
    } else {
        out << Qt::endl
            << "The test reads everything first and saves it to " << snapshotPath << "," << Qt::endl
            << "then changes settings, bands, modes, a keyer memory and a memory channel," << Qt::endl
            << "and puts everything back at the end, even after Ctrl+C." << Qt::endl
            << (tx ? "Transmission checks are ON: the radio will transmit.\n" : "No transmission (add --tx for those checks).\n");
        if (ask(QStringLiteral("Start?")) == QLatin1Char('n')) return 0;
        if (tx && ask(QStringLiteral("A dummy load, or an antenna you may transmit on here, is connected?")) != QLatin1Char('y')) {
            out << "  Transmission checks turned off." << Qt::endl;
            tx = false;
        }
        if (!takeSnapshot()) {
            out << "The snapshot could not be completed: nothing else was changed." << Qt::endl;
            writeReport();
            return 1;
        }
        checkLink();
        checkFrequencyAndModes();
        checkBands();
        checkSplit();
        checkWidthAndShift();
        checkAgc();
        checkRfGain();
        checkClarifier();
        checkCw();
        checkMemories();
        checkMisc();
        if (power) checkPower();
        if (tx) checkTransmit();
        restoreSnapshot();
        if (checklist) runChecklist();
    }

    radio.close();
    pause(200);
    writeReport();
    int fail = 0, pass = 0;
    for (const Result &r : results) {
        if (r.status == QLatin1String("FAIL")) ++fail;
        if (r.status == QLatin1String("PASS")) ++pass;
    }
    out << Qt::endl << pass << " passed, " << fail << " failed. Report: " << reportPath << Qt::endl;
    for (const QString &s : followUps) out << "  To do: " << s << Qt::endl;
    return fail ? 1 : 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<rr::Ft891State>("rr::Ft891State");
    const QStringList a = app.arguments();
    if (a.size() < 2 || a.contains(QStringLiteral("--help"))) {
        out << "usage: ft891-fieldtest PORT [BAUD] [--tx] [--power] [--checklist] [--mem CH] [--batch]\n"
               "       ft891-fieldtest PORT [BAUD] --restore SNAPSHOT.json\n\n"
               "  --tx         also the checks that transmit (dummy load!)\n"
               "  --power      also switch the radio off and on over CAT\n"
               "  --checklist  then the checks outside CAT, asked one by one\n"
               "  --mem CH     memory channel the test may write (default: the last empty one)\n"
               "  --batch      ask nothing: what only the radio can show is left unchecked\n"
               "  --restore F  put the radio back as a snapshot has it\n";
        return 2;
    }
    FieldTest t;
    t.port = a.at(1);
    if (a.size() > 2 && a.at(2).toInt() > 0) t.baud = a.at(2).toInt();
    t.tx = a.contains(QStringLiteral("--tx"));
    t.power = a.contains(QStringLiteral("--power"));
    t.checklist = a.contains(QStringLiteral("--checklist"));
    t.interactive = !a.contains(QStringLiteral("--batch"));
    const int m = a.indexOf(QStringLiteral("--mem"));
    if (m > 0 && m + 1 < a.size()) t.memChannel = a.at(m + 1).toUpper();
    const int r = a.indexOf(QStringLiteral("--restore"));
    if (r > 0 && r + 1 < a.size()) t.restoreFile = a.at(r + 1);
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);
    return t.run();
}

