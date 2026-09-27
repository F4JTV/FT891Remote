#include "audiocodec.h"

#include <cstring>

#ifdef RR_HAVE_OPUS
// Une distribution installe ses en-tetes dans opus/ ; l'arborescence source,
// utilisee sur Android, les range a plat.
#  ifdef RR_OPUS_FLAT_HEADERS
#    include <opus.h>
#  else
#    include <opus/opus.h>
#  endif
#endif

namespace rr {

AudioCodec::AudioCodec() {}

AudioCodec::~AudioCodec()
{
#ifdef RR_HAVE_OPUS
    if (m_enc) opus_encoder_destroy(m_enc);
    if (m_dec) opus_decoder_destroy(m_dec);
#endif
}

bool AudioCodec::opusAvailable()
{
#ifdef RR_HAVE_OPUS
    return true;
#else
    return false;
#endif
}

bool AudioCodec::setCodec(Codec c, int bitrate)
{
#ifdef RR_HAVE_OPUS
    if (m_enc) { opus_encoder_destroy(m_enc); m_enc = nullptr; }
    if (m_dec) { opus_decoder_destroy(m_dec); m_dec = nullptr; }
#else
    if (c == CODEC_OPUS) return false;
#endif
    m_codec = c;

#ifdef RR_HAVE_OPUS
    if (c == CODEC_OPUS) {
        int err = 0;
        // RESTRICTED_LOWDELAY : pas de look-ahead SILK, latence algorithmique ~2,5 ms
        m_enc = opus_encoder_create(kSampleRate, kChannels,
                                    OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
        if (err != OPUS_OK || !m_enc) return false;
        opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(bitrate));
        opus_encoder_ctl(m_enc, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(m_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(m_enc, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(m_enc, OPUS_SET_PACKET_LOSS_PERC(5));
        opus_encoder_ctl(m_enc, OPUS_SET_DTX(0));   // jamais de coupure en phonie

        m_dec = opus_decoder_create(kSampleRate, kChannels, &err);
        if (err != OPUS_OK || !m_dec) return false;
    }
#endif
    return true;
}

QByteArray AudioCodec::encode(const int16_t *pcm)
{
    if (m_codec == CODEC_PCM16)
        return QByteArray(reinterpret_cast<const char *>(pcm), kFrameBytes);

#ifdef RR_HAVE_OPUS
    if (!m_enc) return {};
    QByteArray out(kMaxPayload, Qt::Uninitialized);
    const int n = opus_encode(m_enc, pcm, kFrameSamples,
                              reinterpret_cast<unsigned char *>(out.data()), kMaxPayload);
    if (n <= 0) return {};
    out.resize(n);
    return out;
#else
    return {};
#endif
}

bool AudioCodec::decode(const QByteArray &payload, int16_t *pcmOut)
{
    if (m_codec == CODEC_PCM16) {
        if (payload.size() < kFrameBytes) {
            std::memset(pcmOut, 0, kFrameBytes);   // perte : silence
            return !payload.isEmpty();
        }
        std::memcpy(pcmOut, payload.constData(), kFrameBytes);
        return true;
    }

#ifdef RR_HAVE_OPUS
    if (!m_dec) return false;
    int n;
    if (payload.isEmpty()) {
        // masquage de perte
        n = opus_decode(m_dec, nullptr, 0, pcmOut, kFrameSamples, 0);
    } else {
        n = opus_decode(m_dec, reinterpret_cast<const unsigned char *>(payload.constData()),
                        payload.size(), pcmOut, kFrameSamples, 0);
    }
    if (n != kFrameSamples) {
        std::memset(pcmOut, 0, kFrameBytes);
        return false;
    }
    return true;
#else
    std::memset(pcmOut, 0, kFrameBytes);
    return false;
#endif
}

} // namespace rr
