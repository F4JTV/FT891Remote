// Drives the FT-891 CAT engine against a serial port and checks what comes
// back. Against test/ft891_sim.py it is a regression test of the engine;
// against a real FT-891 it is a first check of a station.
//
//   ft891-cattest /dev/pts/5            simulator
//   ft891-cattest /dev/ttyUSB0 38400    the radio (keep the antenna connected
//                                       or a dummy load: the test keys it)
//   ... --no-tx                         skip everything that transmits
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

#include "../common/catcommands.h"
#include "../common/ft891.h"
#include "../common/protocol.h"
#include "../server/ft891controller.h"

using namespace rr;

namespace {

QTextStream out(stdout);
int failures = 0;

void check(bool ok, const QString &what)
{
    out << (ok ? "  PASS  " : "  FAIL  ") << what << Qt::endl;
    if (!ok) ++failures;
}

// Runs the event loop until cond() holds or the time is up.
template <typename F>
bool waitFor(F cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return cond();
}

void pause(int ms) { waitFor([] { return false; }, ms); }

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<rr::Ft891State>("rr::Ft891State");

    const QStringList args = app.arguments();
    if (args.size() < 2) {
        out << "usage: ft891-cattest PORT [BAUD] [--no-tx] [--verbose]" << Qt::endl;
        return 2;
    }
    const bool noTx = args.contains(QStringLiteral("--no-tx"));
    const bool verbose = args.contains(QStringLiteral("--verbose"));

    Ft891Controller radio;
    Ft891State st;
    QHash<QString, QString> vals;
    QStringList notices;
    QString lastReply;
    bool replied = false;

    QObject::connect(&radio, &Ft891Controller::stateChanged, [&](const Ft891State &s) { st = s; });
    QObject::connect(&radio, &Ft891Controller::valuesChanged, [&](const QVariantMap &d) {
        for (auto it = d.cbegin(); it != d.cend(); ++it) vals.insert(it.key(), it.value().toString());
    });
    QObject::connect(&radio, &Ft891Controller::notice, [&](const QString &n) {
        notices << n;
        if (verbose) out << "  notice: " << n << Qt::endl;
    });
    QObject::connect(&radio, &Ft891Controller::logMessage, [&](const QString &m) {
        if (verbose) out << "  log: " << m << Qt::endl;
    });
    QObject::connect(&radio, &Ft891Controller::catReply, [&](const QString &r) {
        lastReply = r;
        replied = true;
    });

    Ft891Config cfg;
    cfg.catPort = args.at(1);
    cfg.catBaud = args.size() > 2 && args.at(2).toInt() > 0 ? args.at(2).toInt() : 38400;
    cfg.logTraffic = verbose;
    out << "FT-891 CAT engine test on " << cfg.catPort << " at " << cfg.catBaud << " baud" << Qt::endl;
    radio.open(cfg);

    // ------------------------------------------------------------ link
    out << "Link" << Qt::endl;
    { const bool ok_ = waitFor([&] { return st.linkOpen; }, 2000);
      check(ok_, "serial port open"); }
    { const bool ok_ = waitFor([&] { return st.radioOn; }, 4000);
      check(ok_, "the radio answers"); }
    { const bool ok_ = waitFor([&] { return st.radioId == QLatin1String("0650"); }, 2000);
      check(ok_, "ID; gives 0650"); }
    { const bool ok_ = waitFor([&] { return st.freqA > 0 && !st.mode.isEmpty(); }, 2000);
      check(ok_,
          QStringLiteral("IF; parsed: %1 Hz %2 (%3)").arg(st.freqA).arg(st.mode, st.memMode)); }
    { const bool ok_ = waitFor([&] { return st.freqB > 0; }, 3000);
      check(ok_, QStringLiteral("FB; read: %1 Hz").arg(st.freqB)); }

    // --------------------------------------------------- panel read at start
    out << "Front panel" << Qt::endl;
    { const bool ok_ = waitFor([&] { return vals.contains(QStringLiteral("AF")) && vals.contains(QStringLiteral("AGC"))
                               && vals.contains(QStringLiteral("SPEED")) && vals.contains(QStringLiteral("SPL")); },
                  15000);
      check(ok_,
          QStringLiteral("panel groups read in the background (%1 values)").arg(vals.size())); }
    check(vals.contains(QStringLiteral("TNR")),
          QStringLiteral("TNR read through AC; — value « %1 » (the tuner state is its last digit)")
              .arg(vals.value(QStringLiteral("TNR"))));

    // ------------------------------------------------------ frequency, mode
    out << "Frequency and mode" << Qt::endl;
    radio.command(QStringLiteral("freq"), {{QStringLiteral("hz"), 7074000.0}, {QStringLiteral("vfo"), QStringLiteral("A")}});
    { const bool ok_ = waitFor([&] { return st.freqA == 7074000; }, 2000);
      check(ok_, "FA set to 7.074 MHz, IF reads it back"); }
    // A knob turned quickly: only the last position must matter.
    for (int i = 1; i <= 20; ++i)
        radio.command(QStringLiteral("freq"), {{QStringLiteral("hz"), 7074000.0 + i * 10}, {QStringLiteral("vfo"), QStringLiteral("A")}});
    { const bool ok_ = waitFor([&] { return st.freqA == 7074200; }, 2000);
      check(ok_, "20 quick tuning steps end on the last one"); }
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("LSB")}});
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("LSB"); }, 2000);
      check(ok_, "mode LSB"); }
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("PKTUSB")}});
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("DATA-U"); }, 2000);
      check(ok_, "Hamlib name PKTUSB selects DATA-U"); }
    radio.command(QStringLiteral("freq"), {{QStringLiteral("hz"), 14080000.0}, {QStringLiteral("vfo"), QStringLiteral("B")}});
    { const bool ok_ = waitFor([&] { return st.freqB == 14080000; }, 2000);
      check(ok_, "FB set"); }

    // ------------------------------------------------------ mode families
    out << "Mode families" << Qt::endl;
    auto mode = [&](const QString &v) {
        radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), v}});
    };
    auto tuneTo = [&](double hz) {
        radio.command(QStringLiteral("freq"), {{QStringLiteral("hz"), hz}, {QStringLiteral("vfo"), QStringLiteral("A")}});
        waitFor([&] { return st.freqA == quint64(hz); }, 2000);
    };
    tuneTo(7074000);
    mode(QStringLiteral("SSB"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("LSB"); }, 2000);
      check(ok_, QStringLiteral("SSB on 40 m gives LSB (%1)").arg(st.mode)); }
    tuneTo(14074000);
    mode(QStringLiteral("CW"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("CW-U"); }, 2000);
      check(ok_, QStringLiteral("CW the first time gives CW-U (%1)").arg(st.mode)); }
    mode(QStringLiteral("CWR"));                      // an exact mode, as rigctld sends
    waitFor([&] { return st.mode == QLatin1String("CW-L"); }, 2000);
    mode(QStringLiteral("SSB"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("USB"); }, 2000);
      check(ok_, QStringLiteral("SSB on 20 m gives USB (%1)").arg(st.mode)); }
    mode(QStringLiteral("CW"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("CW-L"); }, 2000);
      check(ok_, QStringLiteral("back to CW: the CW-L last used comes back (%1)").arg(st.mode)); }
    mode(QStringLiteral("USB"));
    waitFor([&] { return st.mode == QLatin1String("USB"); }, 2000);
    tuneTo(7074000);                                  // USB kept below 10 MHz
    mode(QStringLiteral("SSB"));
    pause(600);
    check(st.mode == QLatin1String("USB"),
          QStringLiteral("SSB while in USB on 40 m: the radio's sideband is kept (%1)").arg(st.mode));
    mode(QStringLiteral("RTTY"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("RTTY-L"); }, 2000);
      check(ok_, QStringLiteral("RTTY gives RTTY-L (%1)").arg(st.mode)); }
    mode(QStringLiteral("DATA"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("DATA-U"); }, 2000);
      check(ok_, QStringLiteral("DATA gives the DATA-U used before (%1)").arg(st.mode)); }
    mode(QStringLiteral("AM"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("AM"); }, 2000);
      check(ok_, QStringLiteral("AM (%1)").arg(st.mode)); }
    mode(QStringLiteral("FM"));
    { const bool ok_ = waitFor([&] { return st.mode == QLatin1String("FM"); }, 2000);
      check(ok_, QStringLiteral("FM (%1)").arg(st.mode)); }

    // -------------------------------------------------------- band stack
    out << "Band keys" << Qt::endl;
    radio.command(QStringLiteral("band"), {{QStringLiteral("index"), 5}});
    { const bool ok_ = waitFor([&] { return st.freqA >= 14000000 && st.freqA <= 14350000; }, 3000);
      check(ok_,
          QStringLiteral("BS05 goes to 20 m (%1 Hz)").arg(st.freqA)); }

    {
        int mw = -1, m60 = -1;
        const auto &bl = ft891::bands();
        for (int i = 0; i < bl.size(); ++i) {
            if (bl.at(i).name == QLatin1String("MW")) mw = i;
            if (bl.at(i).name == QLatin1String("60")) m60 = i;
        }
        radio.command(QStringLiteral("band"), {{QStringLiteral("index"), mw}});
        { const bool ok_ = waitFor([&] { return st.freqA >= 520000 && st.freqA <= 1710000; }, 3000);
          check(ok_, QStringLiteral("BS12 goes to the MW band (%1 Hz)").arg(st.freqA)); }
        radio.command(QStringLiteral("band"), {{QStringLiteral("index"), m60}});
        { const bool ok_ = waitFor([&] { return st.freqA == 5354000; }, 3000);
          check(ok_, QStringLiteral("60 m has no band-stack key: its preset, 5.354 MHz (%1 Hz)").arg(st.freqA)); }
        radio.command(QStringLiteral("band"), {{QStringLiteral("index"), 5}});
        waitFor([&] { return st.freqA >= 14000000 && st.freqA <= 14350000; }, 3000);
    }

    // ------------------------------------------------------------ settings
    out << "Settings" << Qt::endl;
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("AGC")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("3")}}});
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("AGC")) == QLatin1String("3"); }, 2000);
      check(ok_, "AGC SLOW, read back"); }
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("SFT")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("1"), QStringLiteral("-500")}}});
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("SFT")) == QLatin1String("1-0500"); }, 2000);
      check(ok_, QStringLiteral("IF shift on at −500 Hz, sent as IS01-0500: « %1 »").arg(vals.value(QStringLiteral("SFT")))); }

    // WIDTH: the steps that exist depend on the mode and on NARROW.
    auto setNar = [&](const char *v) {
        radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("NAR")},
                                              {QStringLiteral("v"), QStringList{QString::fromLatin1(v)}}});
        waitFor([&] { return vals.value(QStringLiteral("NAR")) == QLatin1String(v); }, 2000);
    };
    // WIDTH on, then the step: SH0 1 14.
    auto setWidth = [&](const char *step) {
        notices.clear();
        radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("WDH")},
                                              {QStringLiteral("v"), QStringList{QStringLiteral("1"), QString::fromLatin1(step)}}});
    };
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("USB")}});
    waitFor([&] { return st.mode == QLatin1String("USB"); }, 2000);

    auto set1 = [&](const QString &code, const QString &v) {
        radio.command(QStringLiteral("set"), {{QStringLiteral("code"), code},
                                              {QStringLiteral("v"), QStringList{v}}});
    };
    // AGC: set to AUTO, answered AUTO-FAST, -MID or -SLOW.
    set1(QStringLiteral("AGC"), QStringLiteral("4"));
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("AGC")) == QLatin1String("6"); }, 2000);
      const CatCommand *agc = ft891::command(QStringLiteral("AGC"));
      check(ok_ && agc && ft891::displayValue(*agc, vals.value(QStringLiteral("AGC"))) == QLatin1String("AUTO-SLOW"),
            QStringLiteral("AGC AUTO in USB answered 6, shown AUTO-SLOW")); }
    notices.clear();
    set1(QStringLiteral("AGC"), QStringLiteral("6"));
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_, "AUTO-SLOW is an answer only: refused as a setting"); }

    // Clarifier: IF says it is on; menu 05-18 says which side it moves.
    set1(QStringLiteral("EX0518"), QStringLiteral("1"));
    waitFor([&] { return vals.value(QStringLiteral("EX0518")) == QLatin1String("1"); }, 2000);
    set1(QStringLiteral("CLAR"), QStringLiteral("1"));
    { const bool ok_ = waitFor([&] { return st.txClar && !st.rxClar; }, 3000);
      check(ok_, "CLAR on with 05-18 CLAR SELECT TX: the transmit side is shifted, not the receive side"); }
    set1(QStringLiteral("EX0518"), QStringLiteral("0"));
    set1(QStringLiteral("CLAR"), QStringLiteral("0"));
    waitFor([&] { return !st.txClar && !st.rxClar; }, 3000);

    // SPLIT: set with ST, read through RI (RIC); quick split with QS.
    set1(QStringLiteral("SPL"), QStringLiteral("1"));
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("SPL")) == QLatin1String("1"); }, 2000);
      check(ok_, "SPLIT on with ST1, read back through RIC"); }
    set1(QStringLiteral("SPL"), QStringLiteral("0"));
    waitFor([&] { return vals.value(QStringLiteral("SPL")) == QLatin1String("0"); }, 2000);
    set1(QStringLiteral("EX0513"), QStringLiteral("5"));
    waitFor([&] { return vals.value(QStringLiteral("EX0513")) == QLatin1String("+05"); }, 2000);
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("QSPL")}});
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("SPL")) == QLatin1String("1")
                                            && st.freqB == st.freqA + 5000; }, 3000);
      check(ok_, QStringLiteral("quick split: VFO-B at VFO-A + 5 kHz (%1), split on").arg(st.freqB)); }
    set1(QStringLiteral("SPL"), QStringLiteral("0"));
    waitFor([&] { return vals.value(QStringLiteral("SPL")) == QLatin1String("0"); }, 2000);
    setNar("0");
    setWidth("14");
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("WDH")) == QLatin1String("114"); }, 2000);
      check(ok_, QStringLiteral("USB, NARROW off: WIDTH on, step 14 (2400 Hz), sent as SH0114: « %1 »").arg(vals.value(QStringLiteral("WDH")))); }
    setWidth("03");
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_ && vals.value(QStringLiteral("WDH")) == QLatin1String("114"),
            "USB, NARROW off: step 03 refused (09 to 21 only)"); }
    setNar("1");
    setWidth("03");
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("WDH")) == QLatin1String("103"); }, 2000);
      check(ok_, "USB, NARROW on: step 03 (600 Hz) accepted"); }
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("CW-U")}});
    waitFor([&] { return st.mode == QLatin1String("CW-U"); }, 2000);
    setNar("0");
    setWidth("09");
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_, "CW, NARROW off: step 09 refused (10 to 17 only)"); }
    setWidth("17");
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("WDH")) == QLatin1String("117"); }, 2000);
      check(ok_, "CW, NARROW off: step 17 (3000 Hz) accepted"); }
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("AM")}});
    waitFor([&] { return st.mode == QLatin1String("AM"); }, 2000);
    setWidth("00");
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_, "AM: no WIDTH setting, refused"); }
    radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("USB")}});
    waitFor([&] { return st.mode == QLatin1String("USB"); }, 2000);
    notices.clear();
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("AGC")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("9")}}});
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_, "an enumeration value that does not exist is refused"); }
    notices.clear();
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("EX1701")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("0")}}});
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_, "a reset without confirmation is refused"); }
    notices.clear();
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("MOX")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("1")}}});
    { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
      check(ok_, "MOX through « set » is refused by the controller"); }

    // --------------------------------------------------------------- menu
    out << "Menu" << Qt::endl;
    radio.command(QStringLiteral("read"), {{QStringLiteral("group"), QStringLiteral("MENU — GENERAL")}});
    { const bool ok_ = waitFor([&] { return vals.contains(QStringLiteral("EX0520")); }, 8000);
      check(ok_,
          QStringLiteral("menu 05 read (EX0506 CAT RATE = %1)").arg(vals.value(QStringLiteral("EX0506")))); }
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("EX0504")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("30")}}});
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("EX0504")) == QLatin1String("030"); }, 2000);
      check(ok_,
          "05-04 BEEP LEVEL set to 30"); }

    // ---------------------------------------------------- keyer memory
    out << "Keyer memory" << Qt::endl;
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("EX0408")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("0")}}});
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("KM2")},
                                          {QStringLiteral("v"), QStringList{QStringLiteral("test de n0call k")}}});
    { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("KM2")).trimmed() == QLatin1String("TEST DE N0CALL K"); }, 3000);
      check(ok_, QStringLiteral("KM2 written and read back: « %1 »")
                     .arg(vals.value(QStringLiteral("KM2")).trimmed())); }

    // ------------------------------------------------------------ read all
    out << "Read all" << Qt::endl;
    {
        // What Read all must bring: every readable setting outside the
        // frequency and meter groups, and the read-only information.
        QStringList expected;
        for (const CatGroup &g : ft891::description().groups) {
            if (g.poll == QLatin1String("core") || g.poll == QLatin1String("meter")) continue;
            for (const CatCommand &c : g.commands)
                if (c.canRead() && (!c.params.isEmpty() || !c.canSet())) expected << c.code;
        }
        const int settingsExpected = expected.size();
        for (const QString &ch : ft891::memoryChannels()) expected << ft891::memoryKey(ch);
        int total = 0;
        bool finished = false;
        const auto conn = QObject::connect(&radio, &Ft891Controller::readProgress, [&](int done, int t) {
            if (t > 0) total = qMax(total, t);
            if (t == 0 && done == 0 && total > 0) finished = true;
        });
        QElapsedTimer clock;
        clock.start();
        radio.command(QStringLiteral("read"), {{QStringLiteral("all"), true}});
        waitFor([&] { return finished; }, 60000);
        const qint64 ms = clock.elapsed();
        pause(300);                       // the last answers reach the map
        QObject::disconnect(conn);
        QStringList missing;
        for (const QString &code : expected)
            if (!vals.contains(code)) missing << code;
        // The repeater shift is only readable in FM (OS in the CAT reference):
        // in another mode the radio refuses it, and that is not a fault.
        if (ft891::modeFamily(st.mode) != QLatin1String("FM") && missing.removeAll(QStringLiteral("RPT")))
            out << QStringLiteral("        RPT not read in %1: the radio answers it in FM only").arg(st.mode) << Qt::endl;
        out << QStringLiteral("        %1 settings and %2 memories expected, %3 queued by Read all, %4 ms (%5 ms each)")
                   .arg(settingsExpected).arg(expected.size() - settingsExpected).arg(total).arg(ms)
                   .arg(total ? ms / total : 0) << Qt::endl;
        check(finished, "the progress bar reaches its end");
        check(missing.isEmpty(), missing.isEmpty()
              ? QStringLiteral("every setting and every memory has a value from the radio")
              : QStringLiteral("%1 without a value: %2").arg(missing.size()).arg(missing.join(QLatin1Char(' '))));
        for (const QString &code : {QStringLiteral("EX1801"), QStringLiteral("EX1802"), QStringLiteral("EX1803")})
            check(vals.contains(code), QStringLiteral("%1 read: « %2 »").arg(code, vals.value(code)));
    }

    // ------------------------------------------------------------ memories
    out << "Memories" << Qt::endl;
    {
        auto mem = [&](const char *ch) { return vals.value(ft891::memoryKey(QString::fromLatin1(ch))); };
        const ft891::IfInfo m1 = ft891::parseIf(QStringLiteral("IF") + mem("001"));
        check(m1.ok && m1.freq == 14074000 && m1.mode == QLatin1Char('2'),
              QStringLiteral("memory 001 read with MR: %1 Hz, mode %2").arg(m1.freq).arg(m1.mode));
        // 099: a channel this test never writes, so that a second run
        // against the same simulator finds it empty too.
        check(vals.contains(ft891::memoryKey(QStringLiteral("099"))) && mem("099").isEmpty(),
              "memory 099, refused by the radio, is an empty channel");
        const ft891::IfInfo p1 = ft891::parseIf(QStringLiteral("IF") + mem("P1L"));
        check(p1.ok && p1.freq == 7000000, "PMS channel P1L read");
        // Every memory under its own channel, with its own frequency: a late
        // answer must not be filed under the channel read next.
        const struct { const char *ch; quint64 hz; } held[] = {
            {"002", 7074000}, {"003", 10136000}, {"010", 29600000}, {"025", 50313000}, {"P1U", 7040000}};
        QStringList wrong;
        for (const auto &h : held) {
            const ft891::IfInfo m = ft891::parseIf(QStringLiteral("IF") + mem(h.ch));
            if (!m.ok || m.freq != h.hz) wrong << QStringLiteral("%1=%2").arg(QString::fromLatin1(h.ch)).arg(m.freq);
        }
        for (const char *e : {"004", "005", "011", "098"})
            if (!vals.contains(ft891::memoryKey(QString::fromLatin1(e))) || !mem(e).isEmpty()) wrong << QString::fromLatin1(e);
        check(wrong.isEmpty(), wrong.isEmpty() ? QStringLiteral("every memory under its own channel")
                                               : QStringLiteral("memories misfiled or missing: %1").arg(wrong.join(QLatin1Char(' '))));

        radio.command(QStringLiteral("memwrite"), {{QStringLiteral("ch"), QStringLiteral("050")},
                                                  {QStringLiteral("hz"), 21074000},
                                                  {QStringLiteral("mode"), QStringLiteral("DATA-U")},
                                                  {QStringLiteral("clarOn"), true},
                                                  {QStringLiteral("clar"), -120},
                                                  {QStringLiteral("tone"), 0},
                                                  {QStringLiteral("shift"), 0}});
        { const bool ok_ = waitFor([&] {
              const ft891::IfInfo w = ft891::parseIf(QStringLiteral("IF") + mem("050"));
              return w.ok && w.freq == 21074000 && w.mode == QLatin1Char('C') && w.clarOn && w.clarOffset == -120; }, 3000);
          check(ok_, QStringLiteral("memory 050 written with MW and read back: « %1 »").arg(mem("050"))); }
        notices.clear();
        radio.command(QStringLiteral("memwrite"), {{QStringLiteral("ch"), QStringLiteral("051")},
                                                  {QStringLiteral("hz"), 60000000},
                                                  {QStringLiteral("mode"), QStringLiteral("USB")}});
        { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1000);
          check(ok_, "a frequency the radio does not cover is refused before MW"); }

        radio.command(QStringLiteral("memrecall"), {{QStringLiteral("ch"), QStringLiteral("050")}});
        { const bool ok_ = waitFor([&] { return st.memMode == QLatin1String("MEM") && st.memChannel == 50
                                                && st.freqA == 21074000; }, 3000);
          check(ok_, QStringLiteral("memory 050 recalled with MC: %1 %2, %3 Hz").arg(st.memMode).arg(st.memChannel).arg(st.freqA)); }
        radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("VM")}});
        waitFor([&] { return st.memMode == QLatin1String("VFO"); }, 3000);
    }

    // -------------------------------------------------------- clarifier
    out << "Clarifier" << Qt::endl;
    radio.command(QStringLiteral("clar"), {{QStringLiteral("delta"), 120}});
    radio.command(QStringLiteral("clar"), {{QStringLiteral("delta"), 30}});
    { const bool ok_ = waitFor([&] { return st.clarOffset == 150; }, 2000);
      check(ok_,
          QStringLiteral("RU twice: +150 Hz (%1)").arg(st.clarOffset)); }
    radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("CLAR-CLR")}});
    { const bool ok_ = waitFor([&] { return st.clarOffset == 0; }, 2000);
      check(ok_, "RC clears it"); }

    // ------------------------------------------------------ raw terminal
    out << "Raw CAT" << Qt::endl;
    replied = false;
    radio.sendCatString(QStringLiteral("ID;"), true);
    { const bool ok_ = waitFor([&] { return replied; }, 1500);
      check(ok_ && lastReply == QLatin1String("ID0650;"),
          QStringLiteral("terminal ID; answers « %1 »").arg(lastReply)); }
    replied = false;
    radio.sendCatString(QStringLiteral("TX1;"), false);
    { const bool ok_ = waitFor([&] { return replied; }, 1500);
      check(ok_ && lastReply.contains(QLatin1String("refused")),
          "terminal TX1; is refused"); }

    if (!noTx) {
        // --------------------------------------------------------- PTT
        out << "Transmit" << Qt::endl;
        radio.setPtt(true);
        { const bool ok_ = waitFor([&] { return st.ptt; }, 2000);
      check(ok_, "PTT on, TX; reads 1"); }
        { const bool ok_ = waitFor([&] { return st.po > 0; }, 3000);
      check(ok_, QStringLiteral("power meter in transmit (raw %1)").arg(st.po)); }
        radio.setPtt(false);
        { const bool ok_ = waitFor([&] { return !st.ptt; }, 2000);
      check(ok_, "PTT off"); }

        // --------------------------------------------------------- keyer
        out << "Keyer" << Qt::endl;
        radio.command(QStringLiteral("mode"), {{QStringLiteral("v"), QStringLiteral("CW-U")}});
        waitFor([&] { return st.mode == QLatin1String("CW-U"); }, 2000);
        bool busy = false;
        QObject::connect(&radio, &Ft891Controller::morseBusy, [&](bool b) { busy = b; });
        radio.sendMorse(QStringLiteral("test de n0call"));
        { const bool ok_ = waitFor([&] { return busy; }, 1000);
      check(ok_, "text accepted by the keyer path"); }
        { const bool ok_ = waitFor([&] { return vals.value(QStringLiteral("EX0407")) == QLatin1String("0"); }, 2000);
      check(ok_,
              "04-07 CW MEMORY 1 is TEXT"); }
        { const bool ok_ = waitFor([&] { return st.ptt; }, 3000);
      check(ok_, "the radio keys the memory (KM1 + KY6)"); }
        radio.stopMorse();
        { const bool ok_ = waitFor([&] { return !busy; }, 1000);
      check(ok_, "stopped"); }
        waitFor([&] { return !st.ptt; }, 5000);
        // The radio refuses KY1-KY5 for a TEXT memory: that is what a
        // real FT-891 answered.
        notices.clear();
        radio.sendCatString(QStringLiteral("KY2;"), false);
        { const bool ok_ = waitFor([&] { return !notices.isEmpty(); }, 1500);
          check(ok_, "KY2 refused for memory 2 set to TEXT, as the radio does"); }
        waitFor([&] { return !st.ptt; }, 3000);
        // Memory 2 set to MESSAGE: played by its own command, KY2.
        radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("EX0408")},
                                              {QStringLiteral("v"), QStringList{QStringLiteral("1")}}});
        waitFor([&] { return vals.value(QStringLiteral("EX0408")) == QLatin1String("1"); }, 2000);
        radio.command(QStringLiteral("cwmem"), {{QStringLiteral("n"), 2}});
        { const bool ok_ = waitFor([&] { return st.ptt; }, 3000);
          check(ok_, "memory 2 set to MESSAGE plays with KY2"); }
        waitFor([&] { return !st.ptt; }, 6000);
        radio.command(QStringLiteral("set"), {{QStringLiteral("code"), QStringLiteral("EX0408")},
                                              {QStringLiteral("v"), QStringList{QStringLiteral("0")}}});
    }

    // ---------------------------------------------------------- close
    radio.close();
    pause(200);
    out << Qt::endl << (failures ? QStringLiteral("%1 failure(s)").arg(failures) : QStringLiteral("All checks passed"))
        << Qt::endl;
    return failures ? 1 : 0;
}
