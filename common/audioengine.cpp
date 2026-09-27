#include "audioengine.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QHash>
#include <QStringList>

#include <portaudio.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_WIN32) && defined(__has_include)
#  if __has_include(<pa_win_wasapi.h>)
#    include <pa_win_wasapi.h>
#    define RR_HAVE_WASAPI 1
#  endif
#endif

namespace rr {

static bool g_paInit = false;

bool AudioEngine::initialiseLibrary()
{
    if (g_paInit) return true;
    g_paInit = (Pa_Initialize() == paNoError);
    return g_paInit;
}

bool AudioEngine::rescanDevices()
{
    if (g_paInit) {
        Pa_Terminate();
        g_paInit = false;
    }
    return initialiseLibrary();
}

void AudioEngine::terminateLibrary()
{
    if (g_paInit) { Pa_Terminate(); g_paInit = false; }
}

AudioEngine::AudioEngine() { initialiseLibrary(); }
AudioEngine::~AudioEngine() { stopAll(); }

// ---------------------------------------------------------------- parametres
namespace {

// Assemble les parametres d'ouverture d'un peripherique. Sous Windows, si le
// backend est WASAPI, on demande la conversion automatique : la carte peut
// rester bloquee sur 44,1 kHz dans le panneau son, Windows convertit lui-meme
// et nous rend du 48 kHz sans surcout notable.
struct ParamBuilder {
    PaStreamParameters p{};
#ifdef RR_HAVE_WASAPI
    PaWasapiStreamInfo wasapi{};
#endif

    ParamBuilder(const ParamBuilder &) = delete;
    ParamBuilder &operator=(const ParamBuilder &) = delete;

    ParamBuilder(int dev, bool input, int channels = 1)
    {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(dev);
        p.device = dev;
        p.channelCount = channels;
        p.sampleFormat = paInt16;
        p.suggestedLatency = d ? (input ? d->defaultLowInputLatency
                                        : d->defaultLowOutputLatency)
                               : 0.02;
        p.hostApiSpecificStreamInfo = nullptr;
#ifdef RR_HAVE_WASAPI
        const PaHostApiInfo *h = d ? Pa_GetHostApiInfo(d->hostApi) : nullptr;
        if (h && h->type == paWASAPI) {
            wasapi.size        = sizeof(PaWasapiStreamInfo);
            wasapi.hostApiType = paWASAPI;
            wasapi.version     = 1;
            wasapi.flags       = paWinWasapiAutoConvert;
            p.hostApiSpecificStreamInfo = &wasapi;
        }
#endif
    }
};

double negotiate(int dev, bool input)
{
    if (!g_paInit || dev < 0) return 0.0;
    const PaDeviceInfo *d = Pa_GetDeviceInfo(dev);
    if (!d) return 0.0;

    ParamBuilder pb(dev, input);
    const PaStreamParameters *in  = input ? &pb.p : nullptr;
    const PaStreamParameters *out = input ? nullptr : &pb.p;

    // 48 kHz d'abord : c'est le debit du protocole, d'Opus, et de tous les
    // logiciels numeriques. Aucun reechantillonnage s'il passe.
    if (Pa_IsFormatSupported(in, out, double(AudioEngine::kAudioRate)) == paFormatIsSupported)
        return double(AudioEngine::kAudioRate);

    // Sinon le debit natif de la carte, puis quelques valeurs classiques.
    if (d->defaultSampleRate > 0.0 &&
        Pa_IsFormatSupported(in, out, d->defaultSampleRate) == paFormatIsSupported)
        return d->defaultSampleRate;

    for (double r : {44100.0, 96000.0, 32000.0, 24000.0, 16000.0, 8000.0}) {
        if (Pa_IsFormatSupported(in, out, r) == paFormatIsSupported) return r;
    }
    return 0.0;
}

QList<AudioDevice> enumerate(bool wantInput, int hostApiIndex)
{
    QList<AudioDevice> list;
    if (!g_paInit) return list;
    const int n = Pa_GetDeviceCount();
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        if (!d) continue;
        if (hostApiIndex >= 0 && d->hostApi != hostApiIndex) continue;
        if (wantInput  && d->maxInputChannels  < 1) continue;
        if (!wantInput && d->maxOutputChannels < 1) continue;

        AudioDevice a;
        a.index  = i;
        // Certains pilotes ALSA rendent un nom vide ou truffe de blancs :
        // une entree illisible dans la liste ne doit jamais arriver.
        a.name   = QString::fromLocal8Bit(d->name).simplified();
        if (a.name.isEmpty()) a.name = QStringLiteral("device %1").arg(i);
        a.maxIn  = d->maxInputChannels;
        a.maxOut = d->maxOutputChannels;
        a.hostApiIndex = d->hostApi;
        a.defaultSampleRate = d->defaultSampleRate;
        a.defaultLowLatency = wantInput ? d->defaultLowInputLatency
                                        : d->defaultLowOutputLatency;
        const PaHostApiInfo *h = Pa_GetHostApiInfo(d->hostApi);
        if (h) a.hostApi = QString::fromLocal8Bit(h->name);
        list.append(a);
    }
    return list;
}

} // namespace

static QHash<int, double> g_probeCache;

double AudioEngine::probeRate(int deviceIndex, bool input)
{
    if (deviceIndex < 0) return 0.0;
    const int key = deviceIndex * 2 + (input ? 1 : 0);
    const auto it = g_probeCache.constFind(key);
    if (it != g_probeCache.constEnd()) return *it;
    const double r = negotiate(deviceIndex, input);
    g_probeCache.insert(key, r);
    return r;
}

void AudioEngine::clearProbeCache() { g_probeCache.clear(); }

QString AudioEngine::describeDevices()
{
    initialiseLibrary();
    QStringList out;
    if (!g_paInit) return QStringLiteral("PortAudio failed to start.\n");

    out << QStringLiteral("PortAudio: %1").arg(QString::fromLocal8Bit(Pa_GetVersionText()));
    out << QStringLiteral("Default input  device: %1").arg(Pa_GetDefaultInputDevice());
    out << QStringLiteral("Default output device: %1").arg(Pa_GetDefaultOutputDevice());
    out << QString();

    const int apis = Pa_GetHostApiCount();
    for (int a = 0; a < apis; ++a) {
        const PaHostApiInfo *h = Pa_GetHostApiInfo(a);
        if (!h) continue;
        out << QStringLiteral("[%1] %2   %3 device(s)")
                   .arg(a).arg(QString::fromLocal8Bit(h->name)).arg(h->deviceCount);
        for (int d = 0; d < Pa_GetDeviceCount(); ++d) {
            const PaDeviceInfo *i = Pa_GetDeviceInfo(d);
            if (!i || i->hostApi != a) continue;
            out << QStringLiteral("   %1  in=%2 out=%3  native=%4 Hz  in48k=%5 out48k=%6  %7")
                       .arg(d, 3)
                       .arg(i->maxInputChannels, 3)
                       .arg(i->maxOutputChannels, 3)
                       .arg(int(i->defaultSampleRate), 6)
                       .arg(i->maxInputChannels  > 0 ? QString::number(int(probeRate(d, true)))  : QStringLiteral("-"), 6)
                       .arg(i->maxOutputChannels > 0 ? QString::number(int(probeRate(d, false))) : QStringLiteral("-"), 6)
                       .arg(QString::fromLocal8Bit(i->name));
        }
        out << QString();
    }
    clearProbeCache();
    return out.join('\n') + '\n';
}

QList<AudioDevice> AudioEngine::inputDevices(int hostApiIndex)  { return enumerate(true,  hostApiIndex); }
QList<AudioDevice> AudioEngine::outputDevices(int hostApiIndex) { return enumerate(false, hostApiIndex); }

QList<QPair<int, QString>> AudioEngine::hostApis()
{
    QList<QPair<int, QString>> l;
    if (!g_paInit) return l;
    const int n = Pa_GetHostApiCount();
    for (int i = 0; i < n; ++i) {
        const PaHostApiInfo *h = Pa_GetHostApiInfo(i);
        if (h && h->deviceCount > 0)
            l.append({i, QString::fromLocal8Bit(h->name)});
    }
    return l;
}

int AudioEngine::defaultHostApi() { return g_paInit ? Pa_GetDefaultHostApi() : -1; }
int AudioEngine::defaultInput()   { return g_paInit ? Pa_GetDefaultInputDevice()  : -1; }
int AudioEngine::defaultOutput()  { return g_paInit ? Pa_GetDefaultOutputDevice() : -1; }

// ------------------------------------------------------------------ callbacks
int AudioEngine::inCallback(const void *in, void *, unsigned long frames,
                            const PaStreamCallbackTimeInfo *, unsigned long, void *user)
{
    static_cast<AudioEngine *>(user)->ingestCapture(
        static_cast<const int16_t *>(in), size_t(frames));
    return paContinue;
}

int AudioEngine::outCallback(const void *, void *out, unsigned long frames,
                             const PaStreamCallbackTimeInfo *, unsigned long, void *user)
{
    auto *self = static_cast<AudioEngine *>(user);
    int16_t *dst = static_cast<int16_t *>(out);

    if (!self->m_toneEnabled) {
        self->renderPlayback(dst, size_t(frames));
        return paContinue;
    }

    // Sortie stereo : on rend la modulation en mono dans un tampon de travail,
    // puis on entrelace avec la tonalite. Le tampon est dimensionne a
    // l'ouverture du flux, jamais ici : allouer dans un callback audio est le
    // moyen le plus sur de provoquer des coupures.
    if (self->m_monoScratch.size() < frames)
        self->m_monoScratch.assign(frames, 0);
    self->renderPlayback(self->m_monoScratch.data(), size_t(frames));

    const bool keyed = self->m_toneKeyed.load();
    const double step = 2.0 * 3.14159265358979323846
                        * double(self->m_toneHz) / double(kAudioRate);
    for (unsigned long i = 0; i < frames; ++i) {
        dst[2 * i] = self->m_monoScratch[i];          // gauche : la modulation
        if (keyed) {
            dst[2 * i + 1] = int16_t(22000.0 * std::sin(self->m_tonePhase));
            self->m_tonePhase += step;
            if (self->m_tonePhase > 6.283185307179586) self->m_tonePhase -= 6.283185307179586;
        } else {
            dst[2 * i + 1] = 0;
            self->m_tonePhase = 0.0;
        }
    }
    return paContinue;
}

void AudioEngine::setPttTone(bool enabled, int hz)
{
    m_toneEnabled = enabled;
    m_toneHz = hz > 0 ? hz : 2200;
}

void AudioEngine::setPttToneKeyed(bool keyed) { m_toneKeyed.store(keyed); }

// ------------------------------------------------------------------ ouverture
bool AudioEngine::startCapture(int deviceIndex, int framesPerBuffer)
{
    stopCapture();
    if (!g_paInit || deviceIndex < 0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "PortAudio unavailable");
        return false;
    }

    const double rate = negotiate(deviceIndex, true);
    if (rate <= 0.0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "This input device accepted no sample rate");
        return false;
    }
    m_inDevRate = int(rate + 0.5);
    m_inResamp.init(m_inDevRate, kAudioRate);

    // Le tampon demande est exprime au rythme de la carte : on garde la meme
    // duree qu'a 48 kHz pour ne pas allonger la latence sur une carte lente.
    int frames = framesPerBuffer;
    if (frames > 0 && m_inDevRate != kAudioRate)
        frames = int(double(frames) * double(m_inDevRate) / double(kAudioRate) + 0.5);

    const size_t cap = size_t(frames > 0 ? frames : 4096) + 1024;
    m_inScratch.assign(cap, 0);
    m_inConverted.clear();
    m_inConverted.reserve(m_inResamp.maxOutput(cap) + 64);

    ParamBuilder pb(deviceIndex, true);
    m_inRing.reset();
    m_inResamp.reset();

    PaStream *s = nullptr;
    PaError e = Pa_OpenStream(&s, &pb.p, nullptr, rate,
                              frames > 0 ? static_cast<unsigned long>(frames) : paFramesPerBufferUnspecified,
                              paClipOff, &AudioEngine::inCallback, this);
    if (e != paNoError) { m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e)); return false; }
    e = Pa_StartStream(s);
    if (e != paNoError) {
        Pa_CloseStream(s);
        m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e));
        return false;
    }
    m_inStream = s;
    return true;
}

bool AudioEngine::startPlayback(int deviceIndex, int framesPerBuffer)
{
    stopPlayback();
    if (!g_paInit || deviceIndex < 0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "PortAudio unavailable");
        return false;
    }

    const double rate = negotiate(deviceIndex, false);
    if (rate <= 0.0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "This output device accepted no sample rate");
        return false;
    }
    m_outDevRate = int(rate + 0.5);
    m_outResamp.init(kAudioRate, m_outDevRate);

    int frames = framesPerBuffer;
    if (frames > 0 && m_outDevRate != kAudioRate)
        frames = int(double(frames) * double(m_outDevRate) / double(kAudioRate) + 0.5);

    const size_t cap = size_t(frames > 0 ? frames : 4096) + 1024;
    m_outPulled.assign(m_outResamp.inputNeeded(cap) + 64, 0);
    m_outFifo.clear();
    m_outFifo.reserve(m_outResamp.maxOutput(m_outPulled.size()) * 2 + 8192);
    m_outFifoPos = 0;

    // Deux canaux si la tonalite de PTT est demandee, un seul sinon.
    ParamBuilder pb(deviceIndex, false, m_toneEnabled ? 2 : 1);
    if (m_toneEnabled)
        m_monoScratch.assign(size_t(frames > 0 ? frames : 4096) + 1024, 0);
    m_outRing.reset();
    m_outResamp.reset();

    PaStream *s = nullptr;
    PaError e = Pa_OpenStream(&s, nullptr, &pb.p, rate,
                              frames > 0 ? static_cast<unsigned long>(frames) : paFramesPerBufferUnspecified,
                              paClipOff, &AudioEngine::outCallback, this);
    if (e != paNoError) { m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e)); return false; }
    e = Pa_StartStream(s);
    if (e != paNoError) {
        Pa_CloseStream(s);
        m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e));
        return false;
    }
    m_outStream = s;
    return true;
}

void AudioEngine::stopCapture()
{
    if (!m_inStream) return;
    Pa_StopStream(m_inStream);
    Pa_CloseStream(m_inStream);
    m_inStream = nullptr;
}

void AudioEngine::stopPlayback()
{
    if (!m_outStream) return;
    Pa_StopStream(m_outStream);
    Pa_CloseStream(m_outStream);
    m_outStream = nullptr;
    m_outFifo.clear();
    m_outFifoPos = 0;
}

void AudioEngine::stopAll() { stopCapture(); stopPlayback(); }

} // namespace rr
