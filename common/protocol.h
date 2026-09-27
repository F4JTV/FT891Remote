// FT891Remote - client/server protocol.
//
// Derived from RemoteRig's protocol. The audio datagrams and the framed,
// optionally encrypted JSON control channel are unchanged; what travels on the
// control channel is new. RemoteRig described any radio through Hamlib's
// generic vocabulary. Here the radio is always an FT-891, so the state speaks
// its language: its mode names, its meters, its settings by CAT code.
#pragma once

#include <cstdint>
#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace rr {

// ----------------------------------------------------------------- constants
// A different magic from RemoteRig's 'RRTP': the two programs share their
// framing, and a datagram from one must never be taken for the other's.
constexpr uint32_t kMagic       = 0x31393846u;  // 'F891' on the wire
constexpr uint8_t  kVersion     = 1;
constexpr int      kSampleRate  = 48000;
constexpr int      kChannels    = 1;
constexpr int      kFrameSamples= 480;          // 10 ms at 48 kHz
constexpr int      kFrameBytes  = kFrameSamples * 2;
constexpr int      kMaxPayload  = 4096;
constexpr int      kPttTokenLen = 8;

// Announced in the challenge. A client refuses a server that is not an
// FT891Remote station, rather than showing a front panel that drives nothing.
inline constexpr const char *kAppName = "FT891Remote";

// ------------------------------------------------------------------- headers
enum PktType : uint8_t {
    PKT_AUDIO     = 0,   // payload: encoded audio
    PKT_PTT       = 1,   // payload: token(8) + state(1)
    PKT_KEEPALIVE = 2,   // empty payload, keeps the NAT mapping open
};

enum Codec : uint8_t {
    CODEC_PCM16 = 0,
    CODEC_OPUS  = 1,
};

enum Flags : uint8_t {
    FLAG_ENCRYPTED = 0x01,
    FLAG_TX        = 0x02,   // client to server (microphone audio)
    FLAG_DATA      = 0x04,
};

#pragma pack(push, 1)
struct PktHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  codec;
    uint8_t  flags;
    uint32_t session;
    uint32_t seq;
    uint32_t timestamp;   // in samples
    uint16_t length;      // payload bytes
    uint16_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(PktHeader) == 24, "PktHeader must be 24 bytes");

// ------------------------------------------------------------ radio state
// What the server knows of the radio, beyond its settings. Settings — AF
// gain, noise blanker, every menu entry — travel separately, as a map keyed by
// CAT code, so that a new setting never needs a protocol change.
struct Ft891State {
    bool     linkOpen   = false;   // the serial port is open
    bool     radioOn    = false;   // the radio answers CAT
    bool     ptt        = false;   // transmitting, as the radio reports it
    bool     tuning     = false;   // antenna tuner cycle in progress
    bool     cw         = false;   // a keyer message is being sent
    bool     txAllowed  = true;    // frequency inside a transmit range
    bool     hiSwr      = false;   // the radio raised its high-SWR flag

    quint64  freqA      = 0;       // main display (VFO-A, or the memory)
    quint64  freqB      = 0;       // VFO-B
    QString  mode;                 // FT-891 name: USB, CW-U, DATA-L…
    QString  memMode    = QStringLiteral("VFO");   // VFO, MEM, M-TUNE, QMB…
    int      memChannel = 0;
    // The channel as the radio names it — 012, P1L, EMG — since a PMS
    // channel has no number.
    QString  memName;
    int      clarOffset = 0;       // Hz
    bool     rxClar     = false;
    bool     txClar     = false;
    int      rptShift   = 0;       // 0 simplex, 1 plus, 2 minus
    int      toneMode   = 0;       // 0 off, as reported in IF

    // Raw meter readings, 0 to 255, exactly as the radio gives them. The
    // client turns them into units: the scale belongs with the display.
    int      sMeter     = 0;
    int      po         = 0;
    int      swr        = 0;
    int      alc        = 0;
    int      comp       = 0;

    QString  radioId;              // answer to ID;, 0650 on an FT-891
    QString  portName;
    QString  error;

    QJsonObject toJson() const;
    static Ft891State fromJson(const QJsonObject &o);
    // True when something the operator would see has changed.
    bool differs(const Ft891State &o) const;
};

// -------------------------------------------------------------- framing
// Builds a complete datagram. Empty key: no encryption.
QByteArray buildPacket(PktType type, Codec codec, uint8_t flags,
                       uint32_t session, uint32_t seq, uint32_t timestamp,
                       const QByteArray &payload, const QByteArray &key);

// Decodes a datagram. False if invalid.
bool parsePacket(const QByteArray &datagram, const QByteArray &key,
                 PktHeader *hdrOut, QByteArray *payloadOut);

// TCP frame: length (4 bytes, big endian) + compact JSON, encrypted and
// authenticated when a key is given.
QByteArray frameJson(const QJsonObject &obj, const QByteArray &key, uint64_t counter);
bool       parseJsonFrame(const QByteArray &frame, const QByteArray &key,
                          uint64_t counter, QJsonObject *out);

} // namespace rr

Q_DECLARE_METATYPE(rr::Ft891State)
