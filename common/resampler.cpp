#include "resampler.h"

#include <cmath>
#include <cstring>
#include <numeric>

namespace rr {

// MSVC ne definit pas M_PI sans _USE_MATH_DEFINES.
static constexpr double kPi = 3.14159265358979323846;

static int gcdInt(int a, int b) { while (b) { const int t = a % b; a = b; b = t; } return a; }

void Resampler::init(int inRate, int outRate, int tapsPerPhase)
{
    m_inRate  = inRate  > 0 ? inRate  : 48000;
    m_outRate = outRate > 0 ? outRate : 48000;

    if (m_inRate == m_outRate) {
        m_bypass = true;
        m_h.clear();
        m_delay.clear();
        m_L = m_M = 1;
        m_taps = 1;
        m_phase = 0;
        return;
    }

    m_bypass = false;
    const int g = gcdInt(m_inRate, m_outRate);
    m_L = m_outRate / g;      // interpolation
    m_M = m_inRate  / g;      // decimation
    // En decimation, chaque phase doit couvrir davantage d'echantillons
    // d'entree, sinon le filtre est trop court et laisse passer du repliement.
    const int base = tapsPerPhase < 4 ? 4 : tapsPerPhase;
    m_taps = (m_M > m_L) ? base * ((m_M + m_L - 1) / m_L) : base;

    const int n = m_taps * m_L;
    m_h.assign(size_t(n), 0.0f);

    // Coupure la plus basse des deux Nyquist, ramenee au rythme interpole,
    // avec 5 % de marge pour laisser la bande de transition respirer.
    const double fc = 0.5 / double(m_L > m_M ? m_L : m_M) * 0.95;
    const double center = (n - 1) / 2.0;

    for (int i = 0; i < n; ++i) {
        const double x = double(i) - center;
        double sinc;
        if (std::fabs(x) < 1e-9) {
            sinc = 2.0 * fc;
        } else {
            const double a = 2.0 * kPi * fc * x;
            sinc = 2.0 * fc * std::sin(a) / a;
        }
        const double t = double(i) / double(n - 1);
        const double win = 0.42 - 0.5 * std::cos(2.0 * kPi * t) + 0.08 * std::cos(4.0 * kPi * t);
        m_h[size_t(i)] = float(sinc * win);
    }

    // Gain unite : chaque phase doit sommer a 1, donc le filtre entier a L.
    const double sum = std::accumulate(m_h.begin(), m_h.end(), 0.0,
                                       [](double a, float b) { return a + double(b); });
    if (std::fabs(sum) > 1e-12) {
        const float k = float(double(m_L) / sum);
        for (float &v : m_h) v *= k;
    }

    m_delay.assign(size_t(m_taps), 0.0f);
    m_phase = 0;
}

void Resampler::reset()
{
    std::fill(m_delay.begin(), m_delay.end(), 0.0f);
    m_phase = 0;
}

size_t Resampler::maxOutput(size_t n) const
{
    if (m_bypass) return n;
    return (n * size_t(m_L)) / size_t(m_M) + size_t(m_L) + 2;
}

size_t Resampler::inputNeeded(size_t n) const
{
    if (m_bypass) return n;
    return (n * size_t(m_M)) / size_t(m_L) + size_t(m_M) + 2;
}

void Resampler::process(const int16_t *in, size_t n, std::vector<int16_t> &out)
{
    if (m_bypass) {
        out.insert(out.end(), in, in + n);
        return;
    }

    const int taps = m_taps;
    const int L = m_L;
    const float *h = m_h.data();
    float *delay = m_delay.data();

    for (size_t i = 0; i < n; ++i) {
        std::memmove(delay + 1, delay, size_t(taps - 1) * sizeof(float));
        delay[0] = float(in[i]);

        while (m_phase < L) {
            const float *hp = h + m_phase;
            float acc = 0.0f;
            for (int k = 0; k < taps; ++k) acc += hp[k * L] * delay[k];
            if (acc >  32767.0f) acc =  32767.0f;
            if (acc < -32768.0f) acc = -32768.0f;
            out.push_back(int16_t(acc));
            m_phase += m_M;
        }
        m_phase -= L;
    }
}

} // namespace rr
