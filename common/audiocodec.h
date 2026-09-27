// Encodage/decodage audio. Opus en mode faible latence ou PCM 16 bits brut.
#pragma once

#include <QByteArray>
#include "protocol.h"

struct OpusEncoder;
struct OpusDecoder;

namespace rr {

class AudioCodec {
public:
    AudioCodec();
    ~AudioCodec();

    // bitrate ignore en PCM. Reinitialise les etats internes.
    bool setCodec(Codec c, int bitrate = 48000);
    Codec codec() const { return m_codec; }
    static bool opusAvailable();

    // pcm : kFrameSamples echantillons mono 16 bits.
    QByteArray encode(const int16_t *pcm);

    // Renvoie kFrameSamples echantillons. payload vide => masquage de perte.
    bool decode(const QByteArray &payload, int16_t *pcmOut);

private:
    Codec m_codec = CODEC_PCM16;
#ifdef RR_HAVE_OPUS
    OpusEncoder *m_enc = nullptr;
    OpusDecoder *m_dec = nullptr;
#endif
};

} // namespace rr
