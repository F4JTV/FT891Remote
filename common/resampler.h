// Reechantillonneur polyphase rationnel, sans dependance.
// Sert de tampon entre le debit impose par la carte son et les 48 kHz
// du protocole. Le filtre prototype est un sinc fenetre Blackman : assez
// raide pour ne pas abimer les modes numeriques etroits.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace rr {

class Resampler {
public:
    // tapsPerPhase : 16 donne environ -90 dB de repliement pour 44,1 -> 48 kHz.
    void init(int inRate, int outRate, int tapsPerPhase = 16);
    void reset();

    bool bypass() const { return m_bypass; }
    int  inRate()  const { return m_inRate; }
    int  outRate() const { return m_outRate; }

    // Ajoute les echantillons produits a la fin de `out`.
    void process(const int16_t *in, size_t n, std::vector<int16_t> &out);

    // Majoration du nombre d'echantillons de sortie pour n entrees.
    size_t maxOutput(size_t n) const;

    // Nombre d'entrees a fournir pour obtenir au moins n sorties.
    size_t inputNeeded(size_t n) const;

private:
    bool m_bypass = true;
    int  m_inRate = 48000, m_outRate = 48000;
    int  m_L = 1, m_M = 1, m_taps = 1;
    std::vector<float> m_h;       // m_taps * m_L coefficients
    std::vector<float> m_delay;   // m_taps echantillons, le plus recent en 0
    int  m_phase = 0;
};

} // namespace rr
