// Traitement audio commun aux deux backends, PortAudio et Oboe.
// Gain, mesure de crete, reechantillonnage eventuel et files circulaires :
// rien ici ne depend de la bibliotheque qui pilote la carte son.
#include "audioengine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rr {

void AudioEngine::ingestCapture(const int16_t *src, size_t frames)
{
    if (!src) return;

    if (m_inMuted.load()) {
        m_inPeak.store(0.0f);
        m_inClip.store(false);
        m_inResamp.reset();
        return;
    }
    if (m_inScratch.size() < frames) return;   // securite

    const float gain = m_inGain.load();
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float v = float(src[i]) * gain;
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        m_inScratch[i] = int16_t(v);
        const float a = std::fabs(v) / 32768.0f;
        if (a > peak) peak = a;
    }

    if (m_inResamp.bypass()) {
        m_inRing.write(m_inScratch.data(), frames);
    } else {
        m_inConverted.clear();
        m_inResamp.process(m_inScratch.data(), frames, m_inConverted);
        m_inRing.write(m_inConverted.data(), m_inConverted.size());
    }

    if (peak > m_inPeak.load()) m_inPeak.store(peak);
    // 0,995 plutot que 1,0 : la conversion en entier 16 bits ramene deja
    // les valeurs a la butee avant qu'on les voie ici.
    if (peak >= 0.995f) m_inClip.store(true);
}

void AudioEngine::renderPlayback(int16_t *dst, size_t frames)
{
    if (m_outMuted.load()) {
        std::memset(dst, 0, frames * sizeof(int16_t));
        m_outPeak.store(0.0f);
        m_outClip.store(false);
        m_outFifo.clear();
        m_outFifoPos = 0;
        m_outResamp.reset();
        return;
    }

    if (m_outResamp.bypass()) {
        m_outRing.readOrSilence(dst, frames);
    } else {
        // On complete la file de sortie jusqu'a couvrir la demande. Le tirage
        // depuis l'anneau comble les manques par du silence, donc la boucle
        // se termine toujours.
        while (m_outFifo.size() - m_outFifoPos < frames) {
            const size_t need = frames - (m_outFifo.size() - m_outFifoPos);
            size_t pull = m_outResamp.inputNeeded(need);
            if (pull > m_outPulled.size()) pull = m_outPulled.size();
            if (pull == 0) break;
            m_outRing.readOrSilence(m_outPulled.data(), pull);
            m_outResamp.process(m_outPulled.data(), pull, m_outFifo);
        }

        const size_t have = m_outFifo.size() - m_outFifoPos;
        const size_t take = std::min<size_t>(have, frames);
        if (take) std::memcpy(dst, m_outFifo.data() + m_outFifoPos,
                              take * sizeof(int16_t));
        if (take < frames) std::memset(dst + take, 0, (frames - take) * sizeof(int16_t));
        m_outFifoPos += take;

        // Compactage periodique, sans reallocation.
        if (m_outFifoPos > 0 && m_outFifoPos == m_outFifo.size()) {
            m_outFifo.clear();
            m_outFifoPos = 0;
        } else if (m_outFifoPos > 4096) {
            m_outFifo.erase(m_outFifo.begin(),
                                  m_outFifo.begin() + long(m_outFifoPos));
            m_outFifoPos = 0;
        }
    }

    const float gain = m_outGain.load();
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float v = float(dst[i]) * gain;
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        dst[i] = int16_t(v);
        const float a = std::fabs(v) / 32768.0f;
        if (a > peak) peak = a;
    }
    if (peak > m_outPeak.load()) m_outPeak.store(peak);
    if (peak >= 0.995f) m_outClip.store(true);
}

size_t AudioEngine::readCaptured(int16_t *dst, size_t samples)
{
    return m_inRing.read(dst, samples);
}

void AudioEngine::pushPlayback(const int16_t *src, size_t samples)
{
    // Si la file deborde (horloges des deux machines legerement differentes),
    // on jette les plus anciens echantillons plutot que d'accumuler du retard.
    if (m_outRing.freeSpace() < samples) {
        int16_t drop[480];
        size_t need = samples - m_outRing.freeSpace();
        while (need > 0) {
            const size_t n = need > 480 ? 480 : need;
            const size_t got = m_outRing.read(drop, n);
            if (got == 0) break;
            need -= got;
        }
    }
    m_outRing.write(src, samples);
}

float AudioEngine::captureLevel()  { return m_inPeak.exchange(0.0f); }
float AudioEngine::playbackLevel() { return m_outPeak.exchange(0.0f); }
bool  AudioEngine::captureClipped()  { return m_inClip.exchange(false); }
bool  AudioEngine::playbackClipped() { return m_outClip.exchange(false); }


} // namespace rr
