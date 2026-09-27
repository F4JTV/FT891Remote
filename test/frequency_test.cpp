// The frequency parser: what the operator types, and what the client shows —
// the memory editor fills its field with the radio's grouped form, 7.100.000,
// which must be read back.
#include <QCoreApplication>
#include <QTextStream>

#include "../common/ft891.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int failures = 0;
    struct Case { const char *text; quint64 hz; };
    const Case cases[] = {
        {"7.100.000", 7100000},        // the client's own display form
        {"14.074.000", 14074000},
        {"50.313.000", 50313000},
        {"1.000.000", 1000000},
        {"7.1", 7100000},              // megahertz
        {"14.074", 14074000},
        {"14,074", 14074000},          // decimal comma
        {"7100", 7100000},             // kilohertz
        {"14074000", 14074000},        // hertz
        {"14.074 MHz", 14074000},
        {"7100 kHz", 7100000},
        {"14 074 000", 14074000},      // grouping spaces
        {"7.100.5", 0},                // not groups of three: refused
        {"abc", 0},
        {"", 0},
    };
    for (const Case &c : cases) {
        bool ok = false;
        const quint64 hz = rr::ft891::parseFrequency(QString::fromUtf8(c.text), &ok);
        const bool pass = hz == c.hz && ok == (c.hz != 0);
        out << (pass ? "  PASS  " : "  FAIL  ") << '"' << c.text << "\" -> " << hz << Qt::endl;
        if (!pass) ++failures;
    }
    out << Qt::endl << (failures ? QStringLiteral("%1 failure(s)").arg(failures) : QStringLiteral("All checks passed")) << Qt::endl;
    return failures ? 1 : 0;
}
