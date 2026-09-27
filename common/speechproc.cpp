#include "speechproc.h"

#include <algorithm>
#include <cmath>

namespace rr {

static constexpr double kPi = 3.14159265358979323846;

void Biquad::reset() { m_z1 = m_z2 = 0.0; }

void Biquad::setBypass()
{
    m_b0 = 1.0; m_b1 = m_b2 = m_a1 = m_a2 = 0.0;
    reset();
}

void Biquad::normalise(double b0, double b1, double b2, double a0, double a1, double a2)
{
    m_b0 = b0 / a0; m_b1 = b1 / a0; m_b2 = b2 / a0;
    m_a1 = a1 / a0; m_a2 = a2 / a0;
    reset();
}

// Formules de la RBJ Audio EQ Cookbook.
void Biquad::setHighPass(double fs, double f0, double q)
{
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    normalise((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

void Biquad::setLowPass(double fs, double f0, double q)
{
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    normalise((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

void Biquad::setPeaking(double fs, double f0, double q, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    normalise(1 + alpha * A, -2 * c, 1 - alpha * A,
              1 + alpha / A, -2 * c, 1 - alpha / A);
}

float Biquad::process(float x)
{
    const double in = x;
    const double out = m_b0 * in + m_z1;
    m_z1 = m_b1 * in - m_a1 * out + m_z2;
    m_z2 = m_b2 * in - m_a2 * out;
    return float(out);
}

// --------------------------------------------------------------- traitement
void SpeechProcessor::configure(double sampleRate, const SpeechSettings &s)
{
    m_fs = sampleRate > 0 ? sampleRate : 48000.0;
    m_s = s;

    const double nyquist = m_fs / 2.0;

    m_useHp = s.highPassHz > 20.0 && s.highPassHz < nyquist;
    if (m_useHp) m_hp.setHighPass(m_fs, s.highPassHz); else m_hp.setBypass();

    m_usePresence = std::fabs(s.presenceDb) > 0.1 && s.presenceHz < nyquist;
    // Q modere : une bosse large releve la presence sans coloration nasillarde.
    if (m_usePresence) m_presence.setPeaking(m_fs, s.presenceHz, 0.9, s.presenceDb);
    else               m_presence.setBypass();

    m_useLp = s.lowPassHz > 500.0 && s.lowPassHz < nyquist;
    if (m_useLp) m_lp.setLowPass(m_fs, s.lowPassHz); else m_lp.setBypass();

    // Attaque 5 ms, retour 150 ms : assez lent pour ne pas pomper, assez vif
    // pour tenir les cretes sans ecretage.
    m_attackCoef  = std::exp(-1.0 / (0.005 * m_fs));
    m_releaseCoef = std::exp(-1.0 / (0.150 * m_fs));

    // Compensation automatique : la moitie de la reduction maximale theorique,
    // ce qui remonte le niveau moyen sans saturer les cretes.
    double makeup = s.makeupDb;
    if (std::fabs(makeup) < 0.01 && s.compRatio > 1.0) {
        const double maxRed = (-s.compThreshDb) * (1.0 - 1.0 / s.compRatio);
        makeup = maxRed * 0.5;
    }
    m_makeupLin = std::pow(10.0, makeup / 20.0);

    reset();
}

void SpeechProcessor::reset()
{
    m_hp.reset(); m_presence.reset(); m_lp.reset();
    m_envelope = 0.0;
    m_gainDb = 0.0;
    m_lastReductionDb = 0.0f;
}

void SpeechProcessor::process(int16_t *pcm, size_t n)
{
    if (!m_s.enabled) return;

    const bool compress = m_s.compRatio > 1.0;
    const double knee = 6.0;          // genou souple, en dB
    float worstReduction = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        double x = double(pcm[i]) / 32768.0;

        if (m_useHp)       x = m_hp.process(float(x));
        if (m_usePresence) x = m_presence.process(float(x));
        if (m_useLp)       x = m_lp.process(float(x));

        if (compress) {
            // Detecteur de crete : montee rapide, descente lente.
            const double rect = std::fabs(x);
            m_envelope = rect > m_envelope
                             ? m_attackCoef * m_envelope + (1.0 - m_attackCoef) * rect
                             : m_releaseCoef * m_envelope + (1.0 - m_releaseCoef) * rect;

            const double levelDb = 20.0 * std::log10(std::max(m_envelope, 1e-9));
            const double over = levelDb - m_s.compThreshDb;

            double targetReduction = 0.0;
            if (over >= knee / 2.0) {
                targetReduction = over * (1.0 - 1.0 / m_s.compRatio);
            } else if (over > -knee / 2.0) {
                // Transition quadratique dans le genou : pas de rupture de pente,
                // donc pas de craquement a l'entree en compression.
                const double t = over + knee / 2.0;
                targetReduction = (1.0 - 1.0 / m_s.compRatio) * t * t / (2.0 * knee);
            }

            // Le gain lui-meme est lisse : c'est ce qui evite les artefacts.
            const double targetDb = -targetReduction;
            const double coef = targetDb < m_gainDb ? m_attackCoef : m_releaseCoef;
            m_gainDb = coef * m_gainDb + (1.0 - coef) * targetDb;

            x *= std::pow(10.0, m_gainDb / 20.0) * m_makeupLin;
            worstReduction = std::max(worstReduction, float(-m_gainDb));
        }

        // Ecreteur doux : la courbe et sa derivee restent continues, donc pas
        // d'harmoniques dures comme avec un ecretage franc.
        if (x > 0.7) {
            const double e = x - 0.7;
            x = 0.7 + 0.3 * std::tanh(e / 0.3);
        } else if (x < -0.7) {
            const double e = -x - 0.7;
            x = -(0.7 + 0.3 * std::tanh(e / 0.3));
        }

        const double y = x * 32767.0;
        pcm[i] = int16_t(y > 32767.0 ? 32767.0 : (y < -32768.0 ? -32768.0 : y));
    }

    m_lastReductionDb = worstReduction;
}

} // namespace rr
