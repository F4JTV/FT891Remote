// Mise en forme de la modulation, cote emission.
//
// Un micro-casque a perche souffre de l'effet de proximite : les graves
// remontent fortement, la voix devient sourde et perd en intelligibilite.
// La chaine ci-dessous corrige cela sans ajouter la moindre latence : que des
// biquads recursifs et un compresseur sans anticipation. Aucun echantillon
// n'est retenu, le retard algorithmique est nul.
//
// A ne pas utiliser en modes numeriques : le compresseur deformerait les
// tonalites de FT8, PSK ou VARA.
#pragma once

#include <cstdint>
#include <cstddef>
#include <QMetaType>

namespace rr {

// Section du second ordre, forme directe II transposee.
class Biquad {
public:
    void reset();
    void setBypass();
    void setHighPass(double fs, double f0, double q = 0.707);
    void setLowPass(double fs, double f0, double q = 0.707);
    void setPeaking(double fs, double f0, double q, double gainDb);
    float process(float x);

private:
    void normalise(double b0, double b1, double b2, double a0, double a1, double a2);
    double m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    double m_z1 = 0, m_z2 = 0;
};

struct SpeechSettings {
    bool   enabled      = false;
    double highPassHz   = 300.0;   // 0 pour desactiver
    double presenceHz   = 2000.0;
    double presenceDb   = 6.0;     // 0 pour desactiver
    double lowPassHz    = 3200.0;  // 0 pour desactiver
    double compThreshDb = -18.0;
    double compRatio    = 3.0;     // 1.0 pour desactiver
    double makeupDb     = 0.0;     // 0 = calcul automatique
};

class SpeechProcessor {
public:
    void configure(double sampleRate, const SpeechSettings &s);
    void reset();
    const SpeechSettings &settings() const { return m_s; }

    // Traite sur place. Sans effet si le traitement est desactive.
    void process(int16_t *pcm, size_t n);

    // Reduction de gain instantanee, en dB positifs, pour l'affichage.
    float gainReductionDb() const { return m_lastReductionDb; }

private:
    SpeechSettings m_s;
    double m_fs = 48000.0;
    Biquad m_hp, m_presence, m_lp;
    bool   m_useHp = false, m_usePresence = false, m_useLp = false;

    // Compresseur : detecteur de crete et gain lisse en dB.
    double m_envelope = 0.0;
    double m_gainDb = 0.0;
    double m_attackCoef = 0.0, m_releaseCoef = 0.0;
    double m_makeupLin = 1.0;
    float  m_lastReductionDb = 0.0f;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::SpeechSettings)
