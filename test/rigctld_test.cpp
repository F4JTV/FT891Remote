// The rigctld port of the client, spoken to as fldigi and WSJT-X do. fldigi's
// Hamlib names the VFO in every command ("F VFOA 7030000", "T VFOA 3"); read
// as the value, that name made every frequency 0 and every PTT "off".
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTextStream>

#include "../client/rigctldserver.h"

using namespace rr;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int failures = 0;
    auto check = [&](bool ok, const QString &what) {
        out << (ok ? "  PASS  " : "  FAIL  ") << what << Qt::endl;
        if (!ok) ++failures;
    };

    RigctldServer rig;
    QString lastName;
    QVariantMap lastArgs;
    int pttCount = 0;
    bool lastPtt = false;
    QObject::connect(&rig, &RigctldServer::requestCommand, [&](const QString &n, const QVariantMap &a) { lastName = n; lastArgs = a; });
    QObject::connect(&rig, &RigctldServer::requestPtt, [&](bool on) { lastPtt = on; ++pttCount; });
    Ft891State st;
    st.radioOn = true;
    st.freqA = 7070000;
    st.freqB = 14080000;
    st.mode = QStringLiteral("USB");
    rig.updateState(st);
    const quint16 port = 45321;
    check(rig.start(port, true), "listening");

    QTcpSocket s;
    s.connectToHost(QStringLiteral("127.0.0.1"), port);
    check(s.waitForConnected(2000), "connected");
    auto ask = [&](const QByteArray &line, int lines = 1) {
        lastName.clear();
        lastArgs.clear();
        s.write(line + '\n');
        QByteArray got;
        QElapsedTimer t;
        t.start();
        // Server and socket share this thread's event loop: the answer
        // arrives while events are processed, and is read from the buffer.
        while (got.count('\n') < lines && t.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            got += s.readAll();
        }
        QCoreApplication::processEvents();
        return QString::fromLatin1(got).trimmed();
    };

    out << "fldigi (VFO named in every command)" << Qt::endl;
    check(ask("\\chk_vfo") == QLatin1String("CHKVFO 0"), "\\chk_vfo");
    check(ask("f VFOA") == QLatin1String("7070000"), "f VFOA: the receive frequency");
    check(ask("f VFOB") == QLatin1String("7070000"), "f VFOB: the receive VFO too (the FT-891 receives on A)");
    check(ask("V VFOB") == QLatin1String("RPRT 0") && ask("v") == QLatin1String("VFOA"), "V VFOB accepted, the receive VFO stays A");
    const QString f = ask("F VFOA 7030000.000000");
    check(f == QLatin1String("RPRT 0") && lastName == QLatin1String("freq")
              && lastArgs.value(QStringLiteral("hz")).toDouble() == 7030000.0
              && lastArgs.value(QStringLiteral("vfo")).toString() == QLatin1String("A"),
          QStringLiteral("F VFOA 7030000: VFO-A to 7.030 MHz (%1 %2)").arg(f, lastArgs.value(QStringLiteral("hz")).toString()));
    check(ask("F VFOB 7031000") == QLatin1String("RPRT 0") && lastArgs.value(QStringLiteral("vfo")).toString() == QLatin1String("A"),
          "F VFOB: still the receive VFO");
    check(ask("T VFOA 3") == QLatin1String("RPRT 0") && pttCount == 1 && lastPtt, "T VFOA 3 (data): transmit");
    check(ask("T VFOA 0") == QLatin1String("RPRT 0") && pttCount == 2 && !lastPtt, "T VFOA 0: receive");
    check(ask("M VFOA PKTUSB 0") == QLatin1String("RPRT 0") && lastArgs.value(QStringLiteral("v")).toString() == QLatin1String("DATA-U"),
          "M VFOA PKTUSB 0: DATA-U");
    check(ask("m VFOA", 2).startsWith(QLatin1String("USB")), "m VFOA");
    { bool num = false; ask("l VFOA STRENGTH").toInt(&num); check(num, "l VFOA STRENGTH: a number"); }
    check(ask("\\set_vfo_opt 1") == QLatin1String("RPRT 0"), "\\set_vfo_opt accepted");

    out << "Plain client (no VFO names)" << Qt::endl;
    check(ask("F 14074000") == QLatin1String("RPRT 0") && lastArgs.value(QStringLiteral("hz")).toDouble() == 14074000.0, "F 14074000");
    check(ask("T 1") == QLatin1String("RPRT 0") && lastPtt, "T 1");
    check(ask("T 0") == QLatin1String("RPRT 0") && !lastPtt, "T 0");
    check(ask("I 14075000") == QLatin1String("RPRT 0") && lastArgs.value(QStringLiteral("vfo")).toString() == QLatin1String("B"),
          "I 14075000: split transmit frequency on VFO-B");
    check(ask("i") == QLatin1String("14080000"), "i: VFO-B");

    out << "Refused values" << Qt::endl;
    check(ask("F VFOA abc") == QLatin1String("RPRT -1") && lastName.isEmpty(), "F with no frequency: RPRT -1, nothing sent");
    check(ask("T 7") == QLatin1String("RPRT -1"), "T 7: RPRT -1");

    out << Qt::endl << (failures ? QStringLiteral("%1 failure(s)").arg(failures) : QStringLiteral("All checks passed")) << Qt::endl;
    return failures ? 1 : 0;
}
