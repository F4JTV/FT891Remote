#include "ft891.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace rr {
namespace ft891 {

// ================================================================== modes
const QList<ModeInfo> &modes()
{
    // Front-panel order, which is also the order of the MODE key's cycle.
    // The digit is what MD0 carries; the list is the one in the FT-891 CAT
    // reference, and the one Hamlib's newcat backend uses.
    static const QList<ModeInfo> list = {
        {QLatin1Char('1'), QStringLiteral("LSB"),     QStringLiteral("LSB"),    2700, -1},
        {QLatin1Char('2'), QStringLiteral("USB"),     QStringLiteral("USB"),    2700, +1},
        {QLatin1Char('3'), QStringLiteral("CW-U"),    QStringLiteral("CW"),      500,  0},
        {QLatin1Char('7'), QStringLiteral("CW-L"),    QStringLiteral("CWR"),     500,  0},
        {QLatin1Char('5'), QStringLiteral("AM"),      QStringLiteral("AM"),     6000,  0},
        {QLatin1Char('D'), QStringLiteral("AM-N"),    QStringLiteral("AMN"),    3000,  0},
        {QLatin1Char('4'), QStringLiteral("FM"),      QStringLiteral("FM"),    16000,  0},
        {QLatin1Char('B'), QStringLiteral("FM-N"),    QStringLiteral("FMN"),    9000,  0},
        {QLatin1Char('8'), QStringLiteral("DATA-L"),  QStringLiteral("PKTLSB"), 3000, -1},
        {QLatin1Char('C'), QStringLiteral("DATA-U"),  QStringLiteral("PKTUSB"), 3000, +1},
        {QLatin1Char('A'), QStringLiteral("DATA-FM"), QStringLiteral("PKTFM"), 16000,  0},
        {QLatin1Char('6'), QStringLiteral("RTTY-L"),  QStringLiteral("RTTY"),   2500, -1},
        {QLatin1Char('9'), QStringLiteral("RTTY-U"),  QStringLiteral("RTTYR"),  2500, +1},
    };
    return list;
}

QStringList modeNames()
{
    QStringList out;
    for (const ModeInfo &m : modes()) out << m.name;
    return out;
}

const ModeInfo *modeByCode(QChar code)
{
    const QChar c = code.toUpper();
    for (const ModeInfo &m : modes())
        if (m.code == c) return &m;
    return nullptr;
}

const ModeInfo *modeByName(const QString &name)
{
    const QString n = name.trimmed().toUpper();
    for (const ModeInfo &m : modes())
        if (m.name == n || m.hamlib == n) return &m;
    return nullptr;
}

// ================================================================== bands
quint64 parseFrequency(const QString &text, bool *ok)
{
    if (ok) *ok = false;

    QString t = text.simplified();
    t.remove(QRegularExpression(QStringLiteral("(?i)\\s*(hz|khz|mhz)\\s*$")));
    const bool wasKhz = text.contains(QStringLiteral("kHz"), Qt::CaseInsensitive);
    const bool wasHz  = !wasKhz && text.contains(QStringLiteral("Hz"), Qt::CaseInsensitive)
                        && !text.contains(QStringLiteral("MHz"), Qt::CaseInsensitive);

    // Grouping spaces and apostrophes, and a decimal comma, are accepted.
    t.remove(QLatin1Char(' ')).remove(QLatin1Char('\''));

    // The radio's own display groups the digits with dots — 7.100.000,
    // 14.074.000 — and the client shows frequencies that way, the memory
    // editor included. Two dots or more, each followed by three digits, are
    // groups: the digits are hertz. Read as a decimal, the text was refused,
    // and a memory filled in by the editor could not be written back.
    static const QRegularExpression grouped(QStringLiteral("^\\d{1,3}(\\.\\d{3}){2,}$"));
    if (grouped.match(t).hasMatch()) {
        bool valid = false;
        const quint64 hz = QString(t).remove(QLatin1Char('.')).toULongLong(&valid);
        if (!valid || hz < 1000 || hz > 30000000000ULL) return 0;
        if (ok) *ok = true;
        return hz;
    }
    t.replace(QLatin1Char(','), QLatin1Char('.'));

    bool valid = false;
    const double value = t.toDouble(&valid);
    if (!valid || value <= 0) return 0;

    double hz;
    if (wasHz)        hz = value;
    else if (wasKhz)  hz = value * 1e3;
    // Without a unit the magnitude decides: 14.074 is in megahertz, 14074
    // in kilohertz, 14074000 in hertz. That is how frequencies are typed.
    else if (value < 1000.0)     hz = value * 1e6;
    else if (value < 100000.0)   hz = value * 1e3;
    else                         hz = value;

    if (hz < 1000.0 || hz > 3e10) return 0;
    if (ok) *ok = true;
    return quint64(hz + 0.5);
}

const QStringList &memoryChannels()
{
    static const QStringList list = [] {
        QStringList l;
        for (int i = 1; i <= 99; ++i) l << QStringLiteral("%1").arg(i, 3, 10, QLatin1Char('0'));
        for (int i = 1; i <= 9; ++i) {
            l << QStringLiteral("P%1L").arg(i);
            l << QStringLiteral("P%1U").arg(i);
        }
        return l;
    }();
    return list;
}

bool isMemoryChannel(const QString &ch)
{
    return memoryChannels().contains(ch);
}

QString memoryKey(const QString &ch)
{
    return QStringLiteral("MEM:") + ch;
}

QStringList modeFamilies()
{
    return {QStringLiteral("SSB"), QStringLiteral("CW"), QStringLiteral("AM"),
            QStringLiteral("FM"), QStringLiteral("RTTY"), QStringLiteral("DATA")};
}

bool isModeFamily(const QString &name)
{
    return modeFamilies().contains(name.trimmed().toUpper());
}

QString modeFamily(const QString &mode)
{
    const QString m = mode.toUpper();
    if (m == QLatin1String("LSB") || m == QLatin1String("USB")) return QStringLiteral("SSB");
    if (m.startsWith(QLatin1String("CW")))   return QStringLiteral("CW");
    if (m.startsWith(QLatin1String("AM")))   return QStringLiteral("AM");
    if (m.startsWith(QLatin1String("DATA"))) return QStringLiteral("DATA");
    if (m.startsWith(QLatin1String("FM")))   return QStringLiteral("FM");
    if (m.startsWith(QLatin1String("RTTY"))) return QStringLiteral("RTTY");
    return QString();
}

QString defaultModeOf(const QString &family)
{
    const QString f = family.toUpper();
    if (f == QLatin1String("CW"))   return QStringLiteral("CW-U");
    if (f == QLatin1String("AM"))   return QStringLiteral("AM");
    if (f == QLatin1String("FM"))   return QStringLiteral("FM");
    if (f == QLatin1String("RTTY")) return QStringLiteral("RTTY-L");
    if (f == QLatin1String("DATA")) return QStringLiteral("DATA-U");
    return QStringLiteral("USB");
}

QString ssbSidebandFor(quint64 hz)
{
    const bool sixtyMetres = hz >= 5250000 && hz <= 5450000;
    return (hz >= 10000000 || sixtyMetres) ? QStringLiteral("USB") : QStringLiteral("LSB");
}

const QList<BandInfo> &bands()
{
    // BS numbers from the band keys of the FT-891, 160 m to 6 m then GEN.
    // The preset only serves if the radio refuses BS: the band stack, when it
    // works, returns the operator to where he last was on that band.
    static const QList<BandInfo> list = {
        {QStringLiteral("160"), QStringLiteral("00"),  1840000,  1800000,  2000000},
        {QStringLiteral("80"),  QStringLiteral("01"),  3650000,  3500000,  4000000},
        // The FT-891 has no band-stack key for 60 m (BS02 is unused): the
        // preset frequency is set directly.
        {QStringLiteral("60"),  QString(),             5354000,  5351500,  5366500},
        {QStringLiteral("40"),  QStringLiteral("03"),  7100000,  7000000,  7300000},
        {QStringLiteral("30"),  QStringLiteral("04"), 10130000, 10100000, 10150000},
        {QStringLiteral("20"),  QStringLiteral("05"), 14200000, 14000000, 14350000},
        {QStringLiteral("17"),  QStringLiteral("06"), 18130000, 18068000, 18168000},
        {QStringLiteral("15"),  QStringLiteral("07"), 21250000, 21000000, 21450000},
        {QStringLiteral("12"),  QStringLiteral("08"), 24950000, 24890000, 24990000},
        {QStringLiteral("10"),  QStringLiteral("09"), 28400000, 28000000, 29700000},
        {QStringLiteral("6"),   QStringLiteral("10"), 50200000, 50000000, 54000000},
        {QStringLiteral("GEN"), QStringLiteral("11"),        0,    30000, 56000000},
        {QStringLiteral("MW"),  QStringLiteral("12"),  1000000,   520000,  1710000},
    };
    return list;
}

int bandIndexFor(quint64 hz)
{
    const QList<BandInfo> &b = bands();
    for (int i = 0; i < b.size(); ++i) {
        if (b.at(i).name == QLatin1String("GEN")) continue;
        if (hz >= b.at(i).low && hz <= b.at(i).high) return i;
    }
    return -1;
}

QList<Range> txRanges(int region)
{
    // Allocations common to the three regions first, then what differs.
    QList<Range> r = {
        {10100000, 10150000}, {14000000, 14350000}, {18068000, 18168000},
        {21000000, 21450000}, {24890000, 24990000}, {28000000, 29700000},
    };
    switch (region) {
    case 2:
        r += {{1800000, 2000000}, {3500000, 4000000}, {5330000, 5410000},
              {7000000, 7300000}, {50000000, 54000000}};
        break;
    case 3:
        r += {{1800000, 2000000}, {3500000, 3900000}, {5351500, 5366500},
              {7000000, 7300000}, {50000000, 54000000}};
        break;
    default:   // region 1
        r += {{1810000, 2000000}, {3500000, 3800000}, {5351500, 5366500},
              {7000000, 7200000}, {50000000, 52000000}};
        break;
    }
    std::sort(r.begin(), r.end(), [](const Range &a, const Range &b) { return a.low < b.low; });
    return r;
}

Span occupiedSpan(quint64 carrierHz, const QString &mode)
{
    const ModeInfo *m = modeByName(mode);
    const quint64 w = quint64(m ? m->width : 2700);
    const int side = m ? m->side : 0;
    Span s;
    if (side > 0) {
        s.low = carrierHz;
        s.high = carrierHz + w;
    } else if (side < 0) {
        s.low = carrierHz > w ? carrierHz - w : 0;
        s.high = carrierHz;
    } else {
        s.low = carrierHz > w / 2 ? carrierHz - w / 2 : 0;
        s.high = carrierHz + w / 2;
    }
    return s;
}

namespace {
// Steps 01-21 of the SSB columns, and 01-17 of the CW and RTTY/PSK columns,
// of the SH table in the FT-891 CAT reference.
const int kSsbHz[] = {0, 200, 400, 600, 850, 1100, 1350, 1500, 1650, 1800, 1950,
                      2100, 2200, 2300, 2400, 2500, 2600, 2700, 2800, 2900, 3000, 3200};
const int kNarrowHz[] = {0, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500,
                         800, 1200, 1400, 1700, 2000, 2400, 3000};

enum class WidthColumn { None, Ssb, Cw, RttyPsk };

WidthColumn widthColumn(const QString &mode)
{
    const QString m = mode.toUpper();
    if (m == QLatin1String("LSB") || m == QLatin1String("USB")) return WidthColumn::Ssb;
    if (m.startsWith(QLatin1String("CW"))) return WidthColumn::Cw;
    // The table's RTTY/PSK columns: RTTY, and DATA in its LSB and USB forms.
    if (m.startsWith(QLatin1String("RTTY")) || m == QLatin1String("DATA-L") || m == QLatin1String("DATA-U"))
        return WidthColumn::RttyPsk;
    return WidthColumn::None;   // AM, AM-N, FM, FM-N, DATA-FM
}
} // namespace

WidthRange widthRange(const QString &mode, bool narrow)
{
    WidthRange r;
    switch (widthColumn(mode)) {
    case WidthColumn::Ssb:
        r.available = true;
        r.first = narrow ? 1 : 9;
        r.last = narrow ? 9 : 21;
        r.defaultHz = narrow ? 1500 : 2400;
        break;
    case WidthColumn::Cw:
        r.available = true;
        r.first = narrow ? 1 : 10;
        r.last = narrow ? 10 : 17;
        r.defaultHz = narrow ? 500 : 2400;
        break;
    case WidthColumn::RttyPsk:
        r.available = true;
        r.first = narrow ? 1 : 10;
        r.last = narrow ? 10 : 17;
        r.defaultHz = narrow ? 300 : 500;
        break;
    case WidthColumn::None:
        break;
    }
    return r;
}

bool widthStepValid(const QString &mode, bool narrow, int step)
{
    const WidthRange r = widthRange(mode, narrow);
    return r.available && (step == 0 || (step >= r.first && step <= r.last));
}

int widthHz(const QString &mode, bool narrow, int step)
{
    if (!widthStepValid(mode, narrow, step)) return 0;
    const WidthRange r = widthRange(mode, narrow);
    if (step == 0) return r.defaultHz;
    return widthColumn(mode) == WidthColumn::Ssb ? kSsbHz[step] : kNarrowHz[step];
}

int widthDefaultStep(const QString &mode, bool narrow)
{
    const WidthRange r = widthRange(mode, narrow);
    if (!r.available) return 0;
    for (int s = r.first; s <= r.last; ++s)
        if (widthHz(mode, narrow, s) == r.defaultHz) return s;
    return r.first;
}

bool spanInside(const Span &s, const QList<Range> &ranges)
{
    for (const Range &r : ranges)
        if (s.low >= r.low && s.high <= r.high) return true;
    return false;
}

// ================================================================= meters
namespace {
struct Cal { int raw; double value; };

double interpolate(const Cal *table, int n, int raw)
{
    if (raw <= table[0].raw) return table[0].value;
    for (int i = 1; i < n; ++i) {
        if (raw <= table[i].raw) {
            const double f = double(raw - table[i - 1].raw)
                             / double(table[i].raw - table[i - 1].raw);
            return table[i - 1].value + f * (table[i].value - table[i - 1].value);
        }
    }
    return table[n - 1].value;
}
} // namespace

double sMeterDb(int raw)
{
    static const Cal t[] = {
        {0, -54}, {12, -48}, {27, -42}, {40, -36}, {55, -30}, {65, -24},
        {80, -18}, {95, -12}, {112, -6}, {130, 0}, {150, 10}, {172, 20},
        {190, 30}, {220, 40}, {240, 50}, {255, 60},
    };
    return interpolate(t, int(sizeof t / sizeof t[0]), raw);
}

QString sMeterText(int raw)
{
    const double db = sMeterDb(raw);
    if (db > 0.5) return QStringLiteral("S9+%1").arg(int(std::lround(db / 5.0) * 5));
    return QStringLiteral("S%1").arg(qBound(0, int(std::lround((db + 54.0) / 6.0)), 9));
}

double swrValue(int raw)
{
    static const Cal t[] = {
        {0, 1.0}, {26, 1.2}, {52, 1.5}, {89, 2.0}, {126, 3.0},
        {173, 4.0}, {236, 5.0}, {255, 10.0},
    };
    return interpolate(t, int(sizeof t / sizeof t[0]), raw);
}

double powerWatts(int raw)
{
    static const Cal t[] = {
        {0, 0.0}, {10, 0.8}, {50, 8.0}, {100, 26.0}, {150, 54.0},
        {200, 92.0}, {250, 140.0}, {255, 145.0},
    };
    return interpolate(t, int(sizeof t / sizeof t[0]), raw);
}

double compDb(int raw)
{
    return qBound(0.0, raw * 30.0 / 255.0, 30.0);
}

// ===================================================================== IF
IfInfo parseIf(const QString &frame)
{
    IfInfo info;
    if (!frame.startsWith(QLatin1String("IF"))) return info;

    // « IF » + channel(3) + frequency + clarifier(5) + 7 single fields and a
    // two-digit filler: 18 characters around the frequency. The FT-891 gives
    // nine frequency digits; eight is accepted too, as older Yaesu sets use,
    // so that a different firmware does not blind the display.
    const int freqDigits = frame.size() - 18;
    if (freqDigits != 9 && freqDigits != 8) return info;

    bool ok = false;
    int pos = 2;
    info.memName    = frame.mid(pos, 3);
    info.memChannel = info.memName.toInt();                      pos += 3;
    info.freq = frame.mid(pos, freqDigits).toULongLong(&ok);     pos += freqDigits;
    if (!ok) return info;
    info.clarOffset = frame.mid(pos, 5).toInt(&ok);              pos += 5;
    if (!ok) info.clarOffset = 0;
    info.clarOn = frame.at(pos++) == QLatin1Char('1');   // P4
    pos += 1;                                                    // P5, fixed 0
    info.mode   = frame.at(pos++);
    info.memMode = QString(frame.at(pos++)).toInt();
    info.tone   = QString(frame.at(pos++)).toInt();
    pos += 2;                                                    // fixed 00
    info.shift  = QString(frame.at(pos)).toInt();
    info.ok = true;
    return info;
}

bool clarMovesRx(const QString &clarSelect)
{
    return clarSelect != QLatin1String("1");
}

bool clarMovesTx(const QString &clarSelect)
{
    return clarSelect == QLatin1String("1") || clarSelect == QLatin1String("2");
}

QString memModeName(int p7)
{
    switch (p7) {
    case 0: return QStringLiteral("VFO");
    case 1: return QStringLiteral("MEM");
    case 2: return QStringLiteral("M-TUNE");
    case 3: return QStringLiteral("QMB");
    case 4: return QStringLiteral("QMB-MT");
    case 5: return QStringLiteral("PMS");
    case 6: return QStringLiteral("HOME");
    default: return QStringLiteral("?");
    }
}

// ============================================================ description
const CatRig &description()
{
    static const CatRig rig = CatLibrary::byName(QStringLiteral("Yaesu FT-891"));
    return rig;
}

namespace {
struct PrefixEntry { QString prefix; const CatCommand *cmd; };

struct Index {
    QHash<QString, const CatCommand *> byCode;
    QHash<QString, const CatGroup *>   groupByCode;
    // Keyed by the first two letters, longest prefix first in each list.
    QHash<QString, QList<PrefixEntry>> byLead;
};

const Index &index()
{
    static const Index idx = [] {
        Index out;
        const CatRig &rig = description();
        for (const CatGroup &g : rig.groups) {
            for (const CatCommand &c : g.commands) {
                out.byCode.insert(c.code, &c);
                out.groupByCode.insert(c.code, &g);
                if (!c.canRead()) continue;          // nothing to recognise
                const QString p = c.answerPrefix();
                if (p.size() < 2) continue;
                out.byLead[p.left(2)].append({p, &c});
            }
        }
        for (auto it = out.byLead.begin(); it != out.byLead.end(); ++it)
            std::sort(it->begin(), it->end(), [](const PrefixEntry &a, const PrefixEntry &b) {
                return a.prefix.size() > b.prefix.size();
            });
        return out;
    }();
    return idx;
}
} // namespace

const CatCommand *command(const QString &code) { return index().byCode.value(code, nullptr); }
const CatGroup *groupOf(const QString &code)   { return index().groupByCode.value(code, nullptr); }

const CatCommand *matchAnswer(const QString &frame, QString *value)
{
    if (frame.size() < 2) return nullptr;
    const auto it = index().byLead.constFind(frame.left(2));
    if (it == index().byLead.constEnd()) return nullptr;
    for (const PrefixEntry &e : *it) {
        if (frame.startsWith(e.prefix)) {
            if (value) *value = frame.mid(e.prefix.size());
            return e.cmd;
        }
    }
    return nullptr;
}

QString displayValue(const CatCommand &c, const QString &raw)
{
    if (raw.isNull()) return QStringLiteral("—");
    // Information without parameters — the versions of menu 18 — is shown
    // as the radio gives it; a version, 0123, as the reference writes it,
    // V01-23.
    if (c.params.isEmpty()) {
        const QString t = raw.trimmed();
        if (t.isEmpty()) return QStringLiteral("—");
        static const QRegularExpression fourDigits(QStringLiteral("^\\d{4}$"));
        if (c.name.contains(QLatin1String("VERSION")) && fourDigits.match(t).hasMatch())
            return QStringLiteral("V%1-%2").arg(t.left(2), t.mid(2));
        return t;
    }
    const QStringList parts = c.splitAnswer(raw);
    QStringList out;
    for (int i = 0; i < c.params.size(); ++i) {
        const CatParam &p = c.params.at(i);
        const QString v = parts.value(i);
        switch (p.type) {
        case CatParam::Enum: {
            QString label = v;
            for (const CatParamValue &pv : p.values)
                if (pv.value == v) { label = pv.label; break; }
            out << label;
            break;
        }
        case CatParam::Text:
            out << v.trimmed();
            break;
        case CatParam::Range:
        default: {
            bool ok = false;
            const int n = v.toInt(&ok);
            out << (ok ? p.describe(n) : v);
            break;
        }
        }
    }
    return out.join(QStringLiteral(" · "));
}

QString keyerText(const QString &text)
{
    QString out;
    const QString up = text.toUpper();
    for (const QChar ch : up) {
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || QStringLiteral(" /?.,=+-").contains(ch))
            out += ch;
    }
    return out.simplified();
}

bool validateValues(const CatCommand &c, const QStringList &in,
                    QStringList *out, QString *why)
{
    QStringList formatted;
    if (in.size() != c.params.size()) {
        if (why) *why = QStringLiteral("%1 expects %2 value(s), got %3")
                            .arg(c.code).arg(c.params.size()).arg(in.size());
        return false;
    }
    for (int i = 0; i < c.params.size(); ++i) {
        const CatParam &p = c.params.at(i);
        const QString raw = in.at(i).trimmed();
        switch (p.type) {
        case CatParam::Enum: {
            QString found;
            for (const CatParamValue &v : p.values)
                if (!v.readOnly && (v.value == raw || v.label == raw)) { found = v.value; break; }
            if (found.isNull()) {
                if (why) *why = QStringLiteral("%1: « %2 » is not a value of %3")
                                    .arg(c.code, raw, p.name);
                return false;
            }
            formatted << found;
            break;
        }
        case CatParam::Text: {
            const QString t = keyerText(raw).left(p.maxLength);
            formatted << t;
            break;
        }
        case CatParam::Range:
        default: {
            bool ok = false;
            int n = raw.toInt(&ok);
            if (!ok) {
                if (why) *why = QStringLiteral("%1: « %2 » is not a number").arg(c.code, raw);
                return false;
            }
            if (n < p.min || n > p.max) {
                if (why) *why = QStringLiteral("%1: %2 is outside %3 … %4")
                                    .arg(c.code).arg(n).arg(p.min).arg(p.max);
                return false;
            }
            // The radio rejects a value between two steps; the nearest step
            // is what the operator meant.
            if (p.step > 1)
                n = p.min + int(std::lround(double(n - p.min) / p.step)) * p.step;
            formatted << p.format(QString::number(qBound(p.min, n, p.max)));
            break;
        }
        }
    }
    if (out) *out = formatted;
    return true;
}

} // namespace ft891
} // namespace rr
