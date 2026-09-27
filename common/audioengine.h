// Capture et restitution audio via PortAudio.
//
// Le protocole travaille toujours en 48 kHz mono. La carte son, elle, fait
// ce qu'elle veut : le moteur negocie le meilleur debit disponible et
// reechantillonne a la frontiere si necessaire, de sorte que le reste du
// programme n'ait jamais a s'en soucier.
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <atomic>
#include <vector>
#include "ringbuffer.h"
#include "resampler.h"

// Deux implementations derriere la meme interface :
//   audioengine.cpp        PortAudio, pour Windows, Linux et macOS
//   audioengine_oboe.cpp   Oboe, pour Android
// Seules les fonctions publiques comptent : le reste du programme ignore
// laquelle est compilee.
#if defined(__ANDROID__) && !defined(RR_AUDIO_OBOE)
#  define RR_AUDIO_OBOE 1
#endif
#ifndef RR_AUDIO_OBOE
typedef void PaStream;
struct PaStreamCallbackTimeInfo;
#endif

namespace rr {

struct AudioDevice {
    int     index = -1;
    QString name;
    QString hostApi;
    int     hostApiIndex = -1;
    int     maxIn = 0;
    int     maxOut = 0;
    double  defaultSampleRate = 0.0;
    double  defaultLowLatency = 0.0;
};

class AudioEngine {
public:
    static constexpr int kAudioRate = 48000;

    AudioEngine();
    ~AudioEngine();

    static bool initialiseLibrary();
    // Relit la liste des periphériques. PortAudio fige l'enumeration a
    // l'initialisation : sans cela, une carte USB branchee apres le
    // demarrage reste invisible. A n'appeler qu'a flux fermes.
    static bool rescanDevices();
    static void terminateLibrary();

    // hostApiIndex < 0 : tous les backends confondus.
    static QList<AudioDevice> inputDevices(int hostApiIndex = -1);
    static QList<AudioDevice> outputDevices(int hostApiIndex = -1);
    static QList<QPair<int, QString>> hostApis();
    static int  defaultHostApi();
    static int  defaultInput();
    static int  defaultOutput();

    // Debit qui sera reellement utilise, ou 0 si le peripherique est inutilisable.
    // Le resultat est mis en cache : sonder un peripherique recalcitrant coute
    // huit ouvertures ALSA et autant de lignes d'erreur sur la console.
    static double probeRate(int deviceIndex, bool input);
    static void   clearProbeCache();

    // Inventaire complet sur la sortie standard, pour diagnostic.
    static QString describeDevices();

    bool startCapture(int deviceIndex, int framesPerBuffer = 480);
    bool startPlayback(int deviceIndex, int framesPerBuffer = 480);

    // Tonalite de PTT sur le canal droit. Certaines interfaces — Digirig entre
    // autres — commutent le poste en detectant un signal sur ce canal, ce qui
    // laisse au logiciel la maitrise de l'instant exact de la commutation. La
    // sortie passe alors en stereo : modulation a gauche, tonalite a droite.
    void setPttTone(bool enabled, int hz = 2200);
    void setPttToneKeyed(bool keyed);
    bool pttToneEnabled() const { return m_toneEnabled; }
    void stopCapture();
    void stopPlayback();
    void stopAll();

#ifdef RR_AUDIO_OBOE
    bool captureRunning()  const { return m_inRunning; }
    bool playbackRunning() const { return m_outRunning; }
#else
    bool captureRunning()  const { return m_inStream  != nullptr; }
    bool playbackRunning() const { return m_outStream != nullptr; }
#endif

    // Debit impose par la carte son (48000 si elle a accepte notre demande).
    int  captureDeviceRate()  const { return m_inDevRate; }
    int  playbackDeviceRate() const { return m_outDevRate; }
    bool captureResampled()   const { return m_inDevRate  != kAudioRate; }
    bool playbackResampled()  const { return m_outDevRate != kAudioRate; }

    // Cote application : toujours du 48 kHz mono.
    size_t readCaptured(int16_t *dst, size_t samples);
    size_t capturedAvailable() const { return m_inRing.available(); }
    void   pushPlayback(const int16_t *src, size_t samples);
    size_t playbackQueued() const { return m_outRing.available(); }
    void   flushPlayback() { m_outRing.reset(); }
    void   flushCapture()  { m_inRing.reset(); }

    void setCaptureMuted(bool m)  { m_inMuted.store(m); }
    void setPlaybackMuted(bool m) { m_outMuted.store(m); }
    void setCaptureGain(float g)  { m_inGain.store(g); }
    void setPlaybackGain(float g) { m_outGain.store(g); }

    float captureLevel();
    float playbackLevel();
    // Vrai si des echantillons ont touche la butee depuis le dernier appel.
    // Un niveau de 1,0 ne suffit pas a le dire : un seul echantillon en butee
    // passe inapercu dans une crete lue toutes les 250 ms.
    bool  captureClipped();
    bool  playbackClipped();

    QString lastError() const { return m_lastError; }

private:
    // Traitement partage par les deux backends : gain, mesure de crete,
    // reechantillonnage eventuel et files circulaires. Appele depuis le
    // callback temps reel, donc sans allocation.
    void ingestCapture(const int16_t *src, size_t frames);
    void renderPlayback(int16_t *dst, size_t frames);

#ifdef RR_AUDIO_OBOE
    struct OboeImpl;
    OboeImpl *m_impl = nullptr;
    bool m_inRunning = false;
    bool m_outRunning = false;
#else
    static int inCallback(const void *in, void *out, unsigned long frames,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          unsigned long statusFlags, void *user);
    static int outCallback(const void *in, void *out, unsigned long frames,
                           const PaStreamCallbackTimeInfo *timeInfo,
                           unsigned long statusFlags, void *user);

    PaStream *m_inStream  = nullptr;
    PaStream *m_outStream = nullptr;
#endif
    int m_inDevRate  = kAudioRate;
    int m_outDevRate = kAudioRate;

    RingBuffer m_inRing{kAudioRate};    // 1 s, toujours en 48 kHz
    RingBuffer m_outRing{kAudioRate};

    Resampler m_inResamp;    // debit carte -> 48 kHz
    Resampler m_outResamp;   // 48 kHz -> debit carte

    // Tampons de travail dimensionnes a l'ouverture : aucune allocation
    // ne doit se produire depuis un callback temps reel.
    std::vector<int16_t> m_inScratch, m_inConverted;
    std::vector<int16_t> m_outPulled, m_outConverted, m_outFifo;
    size_t m_outFifoPos = 0;

    std::atomic<bool>  m_inMuted{false};
    std::atomic<bool>  m_outMuted{false};
    std::atomic<float> m_inGain{1.0f};
    std::atomic<float> m_outGain{1.0f};
    std::atomic<float> m_inPeak{0.0f};
    std::atomic<float> m_outPeak{0.0f};
    std::atomic<bool>  m_inClip{false};
    std::atomic<bool>  m_outClip{false};

    // Tonalite de PTT. La phase n'est touchee que par le callback audio.
    bool               m_toneEnabled = false;
    int                m_toneHz = 2200;
    std::atomic<bool>  m_toneKeyed{false};
    double             m_tonePhase = 0.0;
    std::vector<int16_t> m_monoScratch;
    QString m_lastError;
};

} // namespace rr
