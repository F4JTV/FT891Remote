#include "serialports.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSerialPortInfo>

#include <algorithm>

namespace rr {

namespace {

constexpr quint16 kSiliconLabs = 0x10C4;
constexpr quint16 kCp2105 = 0xEA70;

// The USB interface a tty belongs to. For a USB-serial converter the tty's
// "device" is the converter's port and the interface is its parent; for a
// CDC-ACM device it is the interface itself.
int linuxUsbInterface(const QString &portName)
{
#ifdef Q_OS_LINUX
    const QString base = QStringLiteral("/sys/class/tty/%1/device/").arg(portName);
    for (const QString &rel : {QStringLiteral("../bInterfaceNumber"), QStringLiteral("bInterfaceNumber")}) {
        QFile f(base + rel);
        if (!f.open(QIODevice::ReadOnly)) continue;
        bool ok = false;
        const int n = f.readAll().trimmed().toInt(&ok, 16);
        if (ok) return n;
    }
#else
    Q_UNUSED(portName)
#endif
    return -1;
}

bool isCp2105(const PortFacts &p)
{
    return p.hasIds && p.vendorId == kSiliconLabs && p.productId == kCp2105;
}

// Natural order of port names: COM2 before COM10, ttyUSB1 before ttyUSB10.
// Written out rather than left to QCollator, whose numeric mode depends on
// how Qt was built.
bool naturalLess(const QString &a, const QString &b)
{
    auto split = [](const QString &s, QString *prefix, qint64 *number) {
        int i = s.size();
        while (i > 0 && s.at(i - 1).isDigit()) --i;
        *prefix = s.left(i);
        *number = i < s.size() ? s.mid(i).toLongLong() : -1;
    };
    QString pa, pb;
    qint64 na = 0, nb = 0;
    split(a, &pa, &na);
    split(b, &pb, &nb);
    const int c = pa.compare(pb, Qt::CaseInsensitive);
    if (c != 0) return c < 0;
    if (na != nb) return na < nb;
    return a < b;
}

bool mentions(const PortFacts &p, std::initializer_list<const char *> words)
{
    for (const char *w : words) {
        const QString s = QString::fromLatin1(w);
        if (p.description.contains(s, Qt::CaseInsensitive) || p.manufacturer.contains(s, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool isEnhancedKind(PortKind k) { return k == PortKind::Ft891Enhanced || k == PortKind::Cp2105Enhanced; }

} // namespace

QString portNameOf(const QString &label)
{
    QString s = label.trimmed();
    for (const QString &sep : {QStringLiteral(" — "), QStringLiteral("  (")}) {
        const int i = s.indexOf(sep);
        if (i > 0) s = s.left(i);
    }
    return s.trimmed();
}

QList<SerialPortEntry> classifyPorts(const QList<PortFacts> &input, const QString &knownFt891)
{
    QList<PortFacts> ports = input;
    std::sort(ports.begin(), ports.end(), [](const PortFacts &a, const PortFacts &b) {
        return naturalLess(a.name, b.name);
    });

    // The two halves of one device share a serial number; devices are told
    // apart by it, and kept together in the order of their first port.
    QMap<QString, QList<int>> devices;
    QMap<QString, int> deviceRank;
    for (int i = 0; i < ports.size(); ++i) {
        if (!isCp2105(ports.at(i))) continue;
        const QString sn = ports.at(i).serialNumber;
        devices[sn].append(i);
        if (!deviceRank.contains(sn)) deviceRank.insert(sn, deviceRank.size());
    }

    const QString knownSerial = knownFt891.startsWith(QLatin1String("sn:")) ? knownFt891.mid(3) : QString();
    const QString knownPort = knownFt891.startsWith(QLatin1String("port:")) ? knownFt891.mid(5) : QString();

    QList<SerialPortEntry> out;
    QList<int> rankOf;
    out.reserve(ports.size());
    for (int i = 0; i < ports.size(); ++i) {
        const PortFacts &p = ports.at(i);
        SerialPortEntry e;
        e.name = p.name;
        e.serialNumber = p.serialNumber;

        if (isCp2105(p)) {
            bool enhanced = p.description.contains(QLatin1String("Enhanced"), Qt::CaseInsensitive) || p.usbInterface == 0;
            bool standard = p.description.contains(QLatin1String("Standard"), Qt::CaseInsensitive) || p.usbInterface == 1;
            if (!enhanced && !standard) {
                // Nothing says which is which: the chip enumerates its
                // Enhanced port first, so the first of the pair is taken for
                // it — and the label says it is a guess.
                enhanced = devices.value(p.serialNumber).value(0, -1) == i;
                e.guessed = true;
            }

            // The FT-891 this server has talked to: same USB serial number,
            // or — when the system gives none — the very port it answered on.
            const bool known = (!knownSerial.isEmpty() && p.serialNumber == knownSerial)
                               || (!knownPort.isEmpty() && enhanced && p.name == knownPort)
                               || mentions(p, {"FT-891", "FT891"});
            QString device;
            if (known) {
                e.kind = enhanced ? PortKind::Ft891Enhanced : PortKind::Ft891Standard;
                device = QStringLiteral("FT-891");
            } else {
                e.kind = enhanced ? PortKind::Cp2105Enhanced : PortKind::Cp2105Standard;
                // A driver that names its device is believed; otherwise the
                // chip is all that is known.
                device = mentions(p, {"SCU-17", "SCU17"}) ? QStringLiteral("SCU-17") : QStringLiteral("CP2105");
            }
            e.label = p.name + QStringLiteral(" — ") + device
                      + (enhanced ? QStringLiteral(" Enhanced COM") : QStringLiteral(" Standard COM"));
            if (known) e.label += enhanced ? QStringLiteral(" (CAT)") : QStringLiteral(" (PTT, keying)");
            if (e.guessed) e.label += QStringLiteral(" ?");
            // An unidentified pair shows its serial number: with an FT-891
            // and an SCU-17 both plugged in, it is what tells them apart.
            if (!known && !p.serialNumber.isEmpty()) e.label += QStringLiteral(" · SN ") + p.serialNumber;
            rankOf.append(deviceRank.value(p.serialNumber));
        } else if (p.hasIds) {
            e.kind = PortKind::Usb;
            const QString what = !p.description.isEmpty() ? p.description
                               : !p.manufacturer.isEmpty() ? p.manufacturer
                               : QStringLiteral("USB serial adapter");
            e.label = p.name + QStringLiteral(" — ") + what;
            rankOf.append(-1);
        } else {
            e.kind = PortKind::Other;
            e.label = p.name + QStringLiteral(" — ")
                      + (p.description.isEmpty() ? QStringLiteral("serial port") : p.description);
            rankOf.append(-1);
        }
        out.append(e);
    }

    // The known FT-891 first, then other CP2105 devices, each with its
    // Enhanced then Standard port, then other USB adapters, then the rest.
    // Stable: the natural order holds within each group.
    auto group = [](PortKind k) {
        switch (k) {
        case PortKind::Ft891Enhanced:
        case PortKind::Ft891Standard:  return 0;
        case PortKind::Cp2105Enhanced:
        case PortKind::Cp2105Standard: return 1;
        case PortKind::Usb:            return 2;
        default:                       return 3;
        }
    };
    QList<int> order(out.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int ga = group(out.at(a).kind), gb = group(out.at(b).kind);
        if (ga != gb) return ga < gb;
        if (ga <= 1) {
            if (rankOf.at(a) != rankOf.at(b)) return rankOf.at(a) < rankOf.at(b);
            return isEnhancedKind(out.at(a).kind) && !isEnhancedKind(out.at(b).kind);
        }
        return false;
    });
    QList<SerialPortEntry> sorted;
    sorted.reserve(out.size());
    for (int i : order) sorted.append(out.at(i));
    return sorted;
}

QString ft891IdentityOf(const QList<SerialPortEntry> &ports, const QString &portName)
{
    // A path — /dev/serial/by-id/… — is followed to the tty it names.
    QString name = portName;
    if (name.startsWith(QLatin1Char('/'))) {
        const QString target = QFileInfo(name).canonicalFilePath();
        name = QFileInfo(target.isEmpty() ? name : target).fileName();
    }
    for (const SerialPortEntry &p : ports) {
        if (p.name != name) continue;
        if (!isEnhancedKind(p.kind)) return QString();
        return p.serialNumber.isEmpty() ? QStringLiteral("port:") + p.name
                                        : QStringLiteral("sn:") + p.serialNumber;
    }
    return QString();
}

QList<SerialPortEntry> scanSerialPorts(const QString &knownFt891)
{
    QList<PortFacts> facts;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &i : ports) {
        PortFacts f;
        f.name = i.portName();
        f.description = i.description();
        f.manufacturer = i.manufacturer();
        f.serialNumber = i.serialNumber();
        f.hasIds = i.hasVendorIdentifier() && i.hasProductIdentifier();
        f.vendorId = i.vendorIdentifier();
        f.productId = i.productIdentifier();
        if (f.hasIds) f.usbInterface = linuxUsbInterface(f.name);
        facts.append(f);
    }
    return classifyPorts(facts, knownFt891);
}

} // namespace rr
