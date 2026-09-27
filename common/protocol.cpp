#include "protocol.h"
#include "chacha20.h"

#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QtEndian>
#include <cstring>

namespace rr {

QJsonObject Ft891State::toJson() const
{
    QJsonObject o;
    o["linkOpen"]  = linkOpen;
    o["radioOn"]   = radioOn;
    o["ptt"]       = ptt;
    o["tuning"]    = tuning;
    o["cw"]        = cw;
    o["txAllowed"] = txAllowed;
    o["hiSwr"]     = hiSwr;
    o["freqA"]     = double(freqA);
    o["freqB"]     = double(freqB);
    o["mode"]      = mode;
    o["memMode"]   = memMode;
    o["memCh"]     = memChannel;
    o["memName"]   = memName;
    o["clar"]      = clarOffset;
    o["rxClar"]    = rxClar;
    o["txClar"]    = txClar;
    o["rpt"]       = rptShift;
    o["tone"]      = toneMode;
    o["sm"]        = sMeter;
    o["po"]        = po;
    o["swr"]       = swr;
    o["alc"]       = alc;
    o["comp"]      = comp;
    o["id"]        = radioId;
    o["port"]      = portName;
    if (!error.isEmpty()) o["error"] = error;
    return o;
}

Ft891State Ft891State::fromJson(const QJsonObject &o)
{
    Ft891State s;
    s.linkOpen   = o["linkOpen"].toBool();
    s.radioOn    = o["radioOn"].toBool();
    s.ptt        = o["ptt"].toBool();
    s.tuning     = o["tuning"].toBool();
    s.cw         = o["cw"].toBool();
    s.txAllowed  = o.contains("txAllowed") ? o["txAllowed"].toBool() : true;
    s.hiSwr      = o["hiSwr"].toBool();
    s.freqA      = quint64(o["freqA"].toDouble());
    s.freqB      = quint64(o["freqB"].toDouble());
    s.mode       = o["mode"].toString();
    s.memMode    = o["memMode"].toString(QStringLiteral("VFO"));
    s.memChannel = o["memCh"].toInt();
    s.memName    = o["memName"].toString();
    s.clarOffset = o["clar"].toInt();
    s.rxClar     = o["rxClar"].toBool();
    s.txClar     = o["txClar"].toBool();
    s.rptShift   = o["rpt"].toInt();
    s.toneMode   = o["tone"].toInt();
    s.sMeter     = o["sm"].toInt();
    s.po         = o["po"].toInt();
    s.swr        = o["swr"].toInt();
    s.alc        = o["alc"].toInt();
    s.comp       = o["comp"].toInt();
    s.radioId    = o["id"].toString();
    s.portName   = o["port"].toString();
    s.error      = o["error"].toString();
    return s;
}

bool Ft891State::differs(const Ft891State &o) const
{
    return linkOpen != o.linkOpen || radioOn != o.radioOn || ptt != o.ptt
        || tuning != o.tuning || cw != o.cw || txAllowed != o.txAllowed
        || hiSwr != o.hiSwr || freqA != o.freqA || freqB != o.freqB
        || mode != o.mode || memMode != o.memMode || memChannel != o.memChannel || memName != o.memName
        || clarOffset != o.clarOffset || rxClar != o.rxClar || txClar != o.txClar
        || rptShift != o.rptShift || toneMode != o.toneMode
        || sMeter != o.sMeter || po != o.po || swr != o.swr || alc != o.alc
        || comp != o.comp || radioId != o.radioId || portName != o.portName
        || error != o.error;
}

// Deterministic nonce: 4 bytes session | 4 bytes seq | 4 bytes (type<<8|flags).
// Unchanged from RemoteRig.
static void makeNonce(uint8_t n[12], uint32_t session, uint32_t seq,
                      uint8_t type, uint8_t flags)
{
    qToLittleEndian<quint32>(session, n);
    qToLittleEndian<quint32>(seq, n + 4);
    qToLittleEndian<quint32>(quint32(type) << 8 | flags, n + 8);
}

QByteArray buildPacket(PktType type, Codec codec, uint8_t flags,
                       uint32_t session, uint32_t seq, uint32_t timestamp,
                       const QByteArray &payload, const QByteArray &key)
{
    const bool enc = !key.isEmpty();
    if (enc) flags |= FLAG_ENCRYPTED;

    PktHeader h{};
    h.magic     = kMagic;
    h.version   = kVersion;
    h.type      = uint8_t(type);
    h.codec     = uint8_t(codec);
    h.flags     = flags;
    h.session   = session;
    h.seq       = seq;
    h.timestamp = timestamp;
    h.length    = uint16_t(payload.size());

    QByteArray out;
    out.resize(int(sizeof(PktHeader)) + payload.size());
    std::memcpy(out.data(), &h, sizeof(PktHeader));
    std::memcpy(out.data() + sizeof(PktHeader), payload.constData(), size_t(payload.size()));

    if (enc) {
        uint8_t nonce[12];
        makeNonce(nonce, session, seq, uint8_t(type), flags);
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(out.data()) + sizeof(PktHeader),
                            size_t(payload.size()));
    }
    return out;
}

bool parsePacket(const QByteArray &datagram, const QByteArray &key,
                 PktHeader *hdrOut, QByteArray *payloadOut)
{
    if (datagram.size() < int(sizeof(PktHeader))) return false;

    PktHeader h{};
    std::memcpy(&h, datagram.constData(), sizeof(PktHeader));
    if (h.magic != kMagic || h.version != kVersion) return false;
    if (h.length > kMaxPayload) return false;
    if (datagram.size() < int(sizeof(PktHeader)) + int(h.length)) return false;

    QByteArray payload = datagram.mid(int(sizeof(PktHeader)), int(h.length));

    if (h.flags & FLAG_ENCRYPTED) {
        if (key.isEmpty()) return false;
        uint8_t nonce[12];
        makeNonce(nonce, h.session, h.seq, h.type, h.flags);
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(payload.data()), size_t(payload.size()));
    }

    if (hdrOut)     *hdrOut = h;
    if (payloadOut) *payloadOut = payload;
    return true;
}

// ------------------------------------------------------------- control channel
QByteArray frameJson(const QJsonObject &obj, const QByteArray &key, uint64_t counter)
{
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (!key.isEmpty()) {
        uint8_t nonce[12] = {0};
        qToLittleEndian<quint64>(counter, nonce);
        nonce[11] = 0xC7;   // "control" domain
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(body.data()), size_t(body.size()));
        QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
        mac.addData(body);
        body.append(mac.result().left(16));
    }

    QByteArray out;
    out.resize(4);
    qToBigEndian<quint32>(quint32(body.size()), reinterpret_cast<uchar *>(out.data()));
    out.append(body);
    return out;
}

bool parseJsonFrame(const QByteArray &frame, const QByteArray &key,
                    uint64_t counter, QJsonObject *out)
{
    QByteArray body = frame;

    if (!key.isEmpty()) {
        if (body.size() < 17) return false;
        QByteArray tag = body.right(16);
        body.chop(16);
        QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
        mac.addData(body);
        if (mac.result().left(16) != tag) return false;

        uint8_t nonce[12] = {0};
        qToLittleEndian<quint64>(counter, nonce);
        nonce[11] = 0xC7;
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(body.data()), size_t(body.size()));
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    if (out) *out = doc.object();
    return true;
}

} // namespace rr
