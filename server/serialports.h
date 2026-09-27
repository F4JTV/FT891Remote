// Serial ports of the machine, with the FT-891's two ports recognised — but
// only once the radio has proved it is one.
//
// The FT-891's USB port holds a Silicon Labs CP2105: two serial ports that
// share one serial number. Interface 0, the "Enhanced" port, carries CAT;
// interface 1, the "Standard" port, carries RTS/DTR PTT or CW keying. Windows
// names them in the port's description; Linux shows both as "CP2105 Dual USB
// to UART Bridge Controller" and only sysfs tells them apart.
//
// Yaesu's SCU-17 interface uses the same chip, with the same USB identifiers
// and the same descriptions. Nothing in USB tells the two apart, and probing
// is not an option: opening a port raises DTR on Windows, which on an SCU-17
// keys the other radio. So a CP2105 pair is only called an FT-891 once the
// radio has answered ID0650 on it; the server then remembers the pair by its
// USB serial number. Until then it is shown as a CP2105, never preselected.
#pragma once

#include <QList>
#include <QString>

namespace rr {

enum class PortKind {
    Ft891Enhanced,    // CAT of the FT-891 known to this server
    Ft891Standard,    // its PTT and keying lines
    Cp2105Enhanced,   // a CP2105 not yet identified: an FT-891 or an SCU-17
    Cp2105Standard,
    Usb,              // any other USB serial adapter
    Other             // built-in or virtual port
};

// What the system says about one port. Kept apart from QSerialPortInfo so
// that the recognition can be tested without the hardware.
struct PortFacts {
    QString name;            // COM5, ttyUSB0
    QString description;
    QString manufacturer;
    QString serialNumber;
    bool    hasIds = false;
    quint16 vendorId = 0;
    quint16 productId = 0;
    int     usbInterface = -1;   // Linux sysfs; -1 when unknown
};

struct SerialPortEntry {
    QString  name;           // what QSerialPort opens
    QString  label;          // what the lists show
    PortKind kind = PortKind::Other;
    QString  serialNumber;   // USB serial number, shared by a CP2105 pair
    bool     guessed = false;   // Enhanced/Standard deduced from the order
};

// Every port: the known FT-891 first, then other CP2105 pairs, other USB
// adapters, the rest. knownFt891 is what ft891IdentityOf() returned when the
// radio last answered, empty if it never has.
QList<SerialPortEntry> scanSerialPorts(const QString &knownFt891 = QString());

// The recognition itself, on facts gathered elsewhere.
QList<SerialPortEntry> classifyPorts(const QList<PortFacts> &ports,
                                     const QString &knownFt891 = QString());

// How to recognise the FT-891 again, given the port on which it has just
// answered ID0650: "sn:<USB serial>", or "port:<name>" when the system gives
// no serial number. Empty if that port is not a CP2105's Enhanced port — a
// simulator, another adapter. portName may be a path to the port.
QString ft891IdentityOf(const QList<SerialPortEntry> &ports, const QString &portName);

// The port name at the start of a list label: "COM5 — FT-891 Enhanced COM
// (CAT)" gives "COM5". Also reads the "ttyUSB0  (description)" labels older
// versions saved.
QString portNameOf(const QString &label);

} // namespace rr
