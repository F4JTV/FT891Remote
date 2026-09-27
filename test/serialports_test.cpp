// Recognition of the FT-891's serial ports, on port descriptions as Windows
// and Linux report them. No hardware needed.
//
// The FT-891 and Yaesu's SCU-17 use the same CP2105 chip, with the same USB
// identifiers and descriptions: a CP2105 is only called an FT-891 once the
// radio has answered on it (its identity is then passed in).
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "../server/serialports.h"

using namespace rr;

namespace {

QTextStream out(stdout);
int failures = 0;

void check(bool ok, const QString &what)
{
    out << (ok ? "  PASS  " : "  FAIL  ") << what << Qt::endl;
    if (!ok) ++failures;
}

PortFacts cp2105(const QString &name, const QString &description, const QString &serial,
                 int iface = -1)
{
    PortFacts f;
    f.name = name;
    f.description = description;
    f.serialNumber = serial;
    f.hasIds = true;
    f.vendorId = 0x10C4;
    f.productId = 0xEA70;
    f.usbInterface = iface;
    return f;
}

const QString kWinEnh = QStringLiteral("Silicon Labs Dual CP2105 USB to UART Bridge: Enhanced COM Port");
const QString kWinStd = QStringLiteral("Silicon Labs Dual CP2105 USB to UART Bridge: Standard COM Port");
const QString kLinux  = QStringLiteral("CP2105 Dual USB to UART Bridge Controller");

QString dump(const QList<SerialPortEntry> &l)
{
    QStringList s;
    for (const SerialPortEntry &e : l) s << e.label;
    return s.join(QStringLiteral(" | "));
}

const SerialPortEntry *byName(const QList<SerialPortEntry> &l, const QString &name)
{
    for (const SerialPortEntry &e : l)
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    out << "A CP2105 never answered on: shown as a CP2105, not as an FT-891" << Qt::endl;
    {
        PortFacts com1;
        com1.name = QStringLiteral("COM1");
        com1.description = QStringLiteral("Communications Port");
        const auto l = classifyPorts({
            com1,
            cp2105(QStringLiteral("COM10"), kWinStd, QStringLiteral("0123")),
            cp2105(QStringLiteral("COM9"),  kWinEnh, QStringLiteral("0123")),
        });
        out << "    " << dump(l) << Qt::endl;
        check(l.value(0).kind == PortKind::Cp2105Enhanced && l.value(1).kind == PortKind::Cp2105Standard,
              "Enhanced and Standard told apart, device left unnamed");
        check(l.value(0).label == QStringLiteral("COM9 — CP2105 Enhanced COM · SN 0123"),
              "label with the serial number");
        check(!l.value(0).label.contains(QLatin1String("FT-891")) && !l.value(1).label.contains(QLatin1String("FT-891")),
              "nowhere called an FT-891");
    }

    out << "The same pair once the FT-891 has answered on it" << Qt::endl;
    {
        const auto l = classifyPorts({
            cp2105(QStringLiteral("COM10"), kWinStd, QStringLiteral("0123")),
            cp2105(QStringLiteral("COM9"),  kWinEnh, QStringLiteral("0123")),
        }, QStringLiteral("sn:0123"));
        out << "    " << dump(l) << Qt::endl;
        check(l.value(0).kind == PortKind::Ft891Enhanced
                  && l.value(0).label == QStringLiteral("COM9 — FT-891 Enhanced COM (CAT)"),
              "COM9 is the FT-891's CAT port");
        check(l.value(1).kind == PortKind::Ft891Standard
                  && l.value(1).label == QStringLiteral("COM10 — FT-891 Standard COM (PTT, keying)"),
              "COM10 is its Standard port");
    }

    out << "An FT-891 and an SCU-17 plugged in together" << Qt::endl;
    {
        // The SCU-17 has the lower COM numbers: an order-based choice would
        // have picked it.
        const QList<PortFacts> ports = {
            cp2105(QStringLiteral("COM3"), kWinEnh, QStringLiteral("SCU0001")),
            cp2105(QStringLiteral("COM4"), kWinStd, QStringLiteral("SCU0001")),
            cp2105(QStringLiteral("COM7"), kWinEnh, QStringLiteral("FT00891")),
            cp2105(QStringLiteral("COM8"), kWinStd, QStringLiteral("FT00891")),
        };
        const auto before = classifyPorts(ports);
        out << "    before: " << dump(before) << Qt::endl;
        bool anyFt = false;
        for (const auto &e : before)
            anyFt |= e.kind == PortKind::Ft891Enhanced || e.kind == PortKind::Ft891Standard;
        check(!anyFt, "neither pair is taken for the FT-891 before it has answered");

        const auto after = classifyPorts(ports, QStringLiteral("sn:FT00891"));
        out << "    after:  " << dump(after) << Qt::endl;
        check(after.value(0).name == QLatin1String("COM7") && after.value(0).kind == PortKind::Ft891Enhanced,
              "the FT-891's CAT port comes first");
        check(after.value(1).name == QLatin1String("COM8") && after.value(1).kind == PortKind::Ft891Standard,
              "then its Standard port");
        check(byName(after, QStringLiteral("COM3"))->kind == PortKind::Cp2105Enhanced
                  && byName(after, QStringLiteral("COM4"))->kind == PortKind::Cp2105Standard,
              "the SCU-17 stays an unidentified CP2105");
        check(byName(after, QStringLiteral("COM4"))->label.contains(QLatin1String("SN SCU0001")),
              "with its serial number, to tell it apart");
    }

    out << "A driver that names the SCU-17" << Qt::endl;
    {
        PortFacts p = cp2105(QStringLiteral("COM3"), QStringLiteral("SCU-17 Enhanced COM Port"), QStringLiteral("S1"));
        const auto l = classifyPorts({p});
        out << "    " << dump(l) << Qt::endl;
        check(l.value(0).label.startsWith(QStringLiteral("COM3 — SCU-17 Enhanced COM")), "labelled SCU-17");
        check(l.value(0).kind == PortKind::Cp2105Enhanced, "and never an FT-891");
    }

    out << "Linux: sysfs gives the USB interface, whatever the tty numbering" << Qt::endl;
    {
        const auto l = classifyPorts({
            cp2105(QStringLiteral("ttyUSB0"), kLinux, QStringLiteral("00A1B2C3"), 1),
            cp2105(QStringLiteral("ttyUSB1"), kLinux, QStringLiteral("00A1B2C3"), 0),
        }, QStringLiteral("sn:00A1B2C3"));
        out << "    " << dump(l) << Qt::endl;
        check(l.value(0).name == QLatin1String("ttyUSB1") && l.value(0).kind == PortKind::Ft891Enhanced,
              "interface 0 is the Enhanced port, even on ttyUSB1");
        check(l.value(1).name == QLatin1String("ttyUSB0") && l.value(1).kind == PortKind::Ft891Standard,
              "interface 1 is the Standard port");
    }

    out << "Linux without sysfs: the order decides, and the label says so" << Qt::endl;
    {
        const auto l = classifyPorts({
            cp2105(QStringLiteral("ttyUSB1"), kLinux, QStringLiteral("S1")),
            cp2105(QStringLiteral("ttyUSB0"), kLinux, QStringLiteral("S1")),
        });
        out << "    " << dump(l) << Qt::endl;
        check(l.value(0).name == QLatin1String("ttyUSB0") && l.value(0).kind == PortKind::Cp2105Enhanced,
              "the first of the pair is taken for the Enhanced port");
        check(l.value(0).guessed && l.value(0).label.contains(QLatin1String(" ?")), "marked as a guess");
    }

    out << "Identity learnt when the FT-891 answers" << Qt::endl;
    {
        const auto l = classifyPorts({
            cp2105(QStringLiteral("COM9"),  kWinEnh, QStringLiteral("0123")),
            cp2105(QStringLiteral("COM10"), kWinStd, QStringLiteral("0123")),
            cp2105(QStringLiteral("COM11"), kWinEnh, QString()),
        });
        check(ft891IdentityOf(l, QStringLiteral("COM9")) == QLatin1String("sn:0123"), "by USB serial number");
        check(ft891IdentityOf(l, QStringLiteral("COM10")).isEmpty(), "never from a Standard port");
        check(ft891IdentityOf(l, QStringLiteral("COM11")) == QLatin1String("port:COM11"),
              "by port name when the system gives no serial number");
        check(ft891IdentityOf(l, QStringLiteral("/tmp/ft891")).isEmpty(), "not from a simulator's pseudo-terminal");

        // A path is followed to the tty it names, as /dev/serial/by-id/ does.
        QTemporaryDir dir;
        const QString tty = dir.path() + QStringLiteral("/ttyUSB7");
        QFile(tty).open(QIODevice::WriteOnly);
        const QString link = dir.path() + QStringLiteral("/usb-Silicon_Labs_CP2105-if00-port0");
        QFile::link(tty, link);
        const auto l2 = classifyPorts({cp2105(QStringLiteral("ttyUSB7"), kLinux, QStringLiteral("777"), 0)});
        check(ft891IdentityOf(l2, link) == QLatin1String("sn:777"), "through a by-id link");
    }

    out << "Natural order and port names" << Qt::endl;
    {
        PortFacts a, b;
        a.name = QStringLiteral("COM10"); b.name = QStringLiteral("COM2");
        check(classifyPorts({a, b}).value(0).name == QLatin1String("COM2"), "COM2 before COM10");
    }
    check(portNameOf(QStringLiteral("COM5 — FT-891 Enhanced COM (CAT)")) == QLatin1String("COM5"), "current label");
    check(portNameOf(QStringLiteral("COM3 — CP2105 Standard COM · SN 0123")) == QLatin1String("COM3"), "CP2105 label");
    check(portNameOf(QStringLiteral("ttyUSB0  (CP2105 Dual USB to UART Bridge Controller)")) == QLatin1String("ttyUSB0"),
          "label saved by earlier versions");
    check(portNameOf(QStringLiteral("/dev/serial/by-id/usb-Silicon_Labs_CP2105-if00-port0"))
              == QLatin1String("/dev/serial/by-id/usb-Silicon_Labs_CP2105-if00-port0"), "a path is kept whole");

    out << Qt::endl << (failures ? QStringLiteral("%1 failure(s)").arg(failures) : QStringLiteral("All checks passed"))
        << Qt::endl;
    return failures ? 1 : 0;
}
