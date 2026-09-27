// Everything that is specific to the Yaesu FT-891 and shared by the server
// and the client: mode codes, bands, transmit ranges, meter scales, the
// layout of the IF; answer, and the lookup of any answer in the CAT
// description.
//
// The settings themselves are not listed here: they live in
// data/cat/ft891.json, embedded at build time. This file only knows what the
// description cannot say — how to read a composite answer, how a raw meter
// value maps to a unit.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "catcommands.h"

namespace rr {
namespace ft891 {

// ---------------------------------------------------------------- modes
struct ModeInfo {
    QChar   code;       // digit after MD0
    QString name;       // as the radio's display writes it
    QString hamlib;     // name used by rigctld clients (WSJT-X, fldigi…)
    int     width;      // typical occupied bandwidth, Hz, for the band-edge guard
    int     side;       // +1 upper sideband, -1 lower, 0 centred on the carrier
};

const QList<ModeInfo> &modes();
QStringList modeNames();              // in front-panel order
const ModeInfo *modeByCode(QChar code);
// Accepts the FT-891 name or the Hamlib one: « DATA-U » and « PKTUSB » alike.
const ModeInfo *modeByName(const QString &name);

// The six groups the operator chooses from, as on the radio's MODE key:
// SSB, CW, AM, FM, RTTY, DATA. The radio's own mode — LSB or USB, CW-U or
// CW-L, DATA-U… — is what it reports, and what is displayed.
QStringList modeFamilies();
bool isModeFamily(const QString &name);
// « USB » gives « SSB », « CW-L » gives « CW »; empty if unknown.
QString modeFamily(const QString &mode);
// The mode sent when a family is chosen and the radio has not used it yet.
QString defaultModeOf(const QString &family);
// SSB follows the band, as the radio does on its own: LSB below 10 MHz,
// USB from 10 MHz up and on 60 m. CAT cannot ask for "SSB" without a
// sideband, so the server applies the same rule.
QString ssbSidebandFor(quint64 hz);

// ---------------------------------------------------------------- bands
struct BandInfo {
    QString name;
    QString bs;         // two-digit argument of BS;
    quint64 preset;     // fallback frequency if the radio refuses BS
    quint64 low;        // amateur allocation, for display
    quint64 high;
};
const QList<BandInfo> &bands();
int bandIndexFor(quint64 hz);         // -1 outside every amateur band

// Transmit ranges by IARU region (1, 2 or 3). The radio enforces its own
// limits as well; this list only lets the server refuse the PTT first, with
// an explanation, instead of letting the operator key into a refusal.
struct Range { quint64 low; quint64 high; };
QList<Range> txRanges(int region);

struct Span { quint64 low; quint64 high; };
// Spectrum actually occupied: above the carrier in USB, below it in LSB.
Span occupiedSpan(quint64 carrierHz, const QString &mode);
bool spanInside(const Span &s, const QList<Range> &ranges);

// ---------------------------------------------------------------- meters
// The radio reports every meter as 0 to 255. The conversions below follow the
// calibration tables Hamlib uses for the newer Yaesu sets; they are
// approximations, good for a needle, not for a measurement.
double sMeterDb(int raw);             // dB relative to S9
QString sMeterText(int raw);          // « S7 », « S9+20 »
double swrValue(int raw);             // 1.0 and up
double powerWatts(int raw);
double compDb(int raw);

// ------------------------------------------------------------ frequencies
// A frequency as the operator types it, or as the client shows it: 14.074
// (MHz), 14074 (kHz), 14074000 (Hz), "14.074 MHz", "7100 kHz", and the
// radio's grouped form, 7.100.000. 0 and ok false when it is none of those.
quint64 parseFrequency(const QString &text, bool *ok = nullptr);

// ------------------------------------------------------------- memories
// The channels MR reads and MW writes: 001-099, then the PMS pairs P1L-P9U.
// 501-510 (60 m, U.S. and U.K. sets only) and EMG are left out.
const QStringList &memoryChannels();
bool isMemoryChannel(const QString &ch);
// A memory as MR answers it, without "MR": the fields of IF, from the
// channel number on — parseIf(QStringLiteral("IF") + raw) reads it. The
// server stores it under "MEM:<channel>", and an empty channel as an empty
// string.
QString memoryKey(const QString &ch);

// ---------------------------------------------------------------- width
// The WIDTH setting, SH0 followed by a two-digit step. Which steps exist, and
// what bandwidth each one gives, depends on the mode and on NARROW: the table
// of the SH command in the FT-891 CAT Operation Reference Book, copied here.
// Step 00 is the default width of the mode, whatever its value. AM and FM
// have no WIDTH setting.
struct WidthRange {
    bool available = false;   // false in AM, FM and DATA-FM
    int  first = 0;           // steps first..last, plus 00 for the default
    int  last = 0;
    int  defaultHz = 0;       // the bandwidth of step 00
};
WidthRange widthRange(const QString &mode, bool narrow);
// Bandwidth of a step in Hz; 0 if the step does not exist there.
int widthHz(const QString &mode, bool narrow, int step);
bool widthStepValid(const QString &mode, bool narrow, int step);
// The step of the table that gives the same bandwidth as step 00: where a
// slider stands while the radio is on its default width.
int widthDefaultStep(const QString &mode, bool narrow);

// ------------------------------------------------------------------ IF;
struct IfInfo {
    bool    ok = false;
    int     memChannel = 0;      // 0 for a PMS channel or EMG
    QString memName;             // as IF writes it: 012, P1L, EMG
    quint64 freq = 0;
    int     clarOffset = 0;
    // P4: the clarifier is on. Whether it moves reception, transmission or
    // both is menu 05-18 CLAR SELECT, not part of IF (P5 is fixed at 0).
    bool    clarOn = false;
    QChar   mode;
    int     memMode = 0;      // P7
    int     tone = 0;         // P8
    int     shift = 0;        // P10
};
// frame is the answer without its terminator: « IF001014074000+0000002000000 ».
IfInfo parseIf(const QString &frame);
// Which side the clarifier moves, from menu 05-18 CLAR SELECT (0 RX, 1 TX,
// 2 TRX; unknown counts as RX, the radio's default).
bool clarMovesRx(const QString &clarSelect);
bool clarMovesTx(const QString &clarSelect);
QString memModeName(int p7);

// ------------------------------------------------------------ description
// The embedded description, loaded once.
const CatRig &description();
const CatCommand *command(const QString &code);
const CatGroup *groupOf(const QString &code);

// Finds the command an answer belongs to, by the longest answer prefix it
// starts with, and gives back the value that follows the prefix.
// « AG0128 » gives AF and « 128 »; « EX01010200 » gives EX0101 and « 0200 ».
const CatCommand *matchAnswer(const QString &frame, QString *value);

// Human-readable form of a stored value: labels instead of codes, units.
QString displayValue(const CatCommand &c, const QString &raw);

// Checks and formats the values a client sends for a command. Refuses an
// enumeration value the description does not list, rather than silently
// sending the first one — which, for the reset menu, would be « ALL ».
bool validateValues(const CatCommand &c, const QStringList &in,
                    QStringList *out, QString *why);

// Text that the keyer memories accept. The semicolon would end the frame,
// so it can never pass; the rest is limited to what the keyer can send.
QString keyerText(const QString &text);

} // namespace ft891
} // namespace rr
