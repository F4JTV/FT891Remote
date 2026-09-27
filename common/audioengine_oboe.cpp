// Implementation Android du moteur audio, sur Oboe.
//
// PortAudio n'a pas de backend Android en amont : le ticket OpenSL ES ouvert
// en 2011 n'a jamais abouti. Oboe est la bibliotheque recommandee par Google ;
// elle appelle AAudio quand il est disponible et retombe sur OpenSL ES sinon.
//
// L'interface publique de AudioEngine est identique a celle de la version
// PortAudio : clientcore.cpp ne change pas d'une ligne.
#include "audioengine.h"

#ifdef RR_AUDIO_OBOE

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#include <cstring>          // Oboe 1.10 oublie cet include dans FullDuplexStream.h
#include <oboe/Oboe.h>

#include <mutex>

namespace rr {

// Android ne presente pas de liste de peripheriques a la maniere de PortAudio :
// le routage est decide par le systeme, selon ce qui est branche. On expose
// donc une entree unique, et Oboe suit le peripherique par defaut.
static constexpr int kDefaultDeviceId = 0;   // oboe::kUnspecified

struct AudioEngine::OboeImpl : oboe::AudioStreamDataCallback,
                               oboe::AudioStreamErrorCallback {
    AudioEngine *owner = nullptr;
    std::shared_ptr<oboe::AudioStream> input;
    std::shared_ptr<oboe::AudioStream> output;
    std::mutex mutex;
    int framesPerCallback = 480;
    int inputDeviceId = 0;
    int outputDeviceId = 0;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream,
                                          void *audioData, int32_t numFrames) override
    {
        auto *pcm = static_cast<int16_t *>(audioData);
        if (stream->getDirection() == oboe::Direction::Input)
            owner->ingestCapture(pcm, size_t(numFrames));
        else
            owner->renderPlayback(pcm, size_t(numFrames));
        return oboe::DataCallbackResult::Continue;
    }

    // Debranchement du casque, changement de sortie : le flux est ferme par le
    // systeme. Oboe appelle ceci depuis un thread a lui, ou rouvrir est permis.
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result) override
    {
        const bool wasInput = stream->getDirection() == oboe::Direction::Input;
        std::lock_guard<std::mutex> lock(mutex);
        if (wasInput && owner->m_inRunning)
            owner->startCapture(inputDeviceId, framesPerCallback);
        else if (!wasInput && owner->m_outRunning)
            owner->startPlayback(outputDeviceId, framesPerCallback);
    }

    bool open(oboe::Direction dir, int framesPerBuffer, int deviceId, QString *errorOut)
    {
        oboe::AudioStreamBuilder b;
        b.setDirection(dir)
         ->setFormat(oboe::AudioFormat::I16)
         ->setChannelCount(1)
         ->setSampleRate(AudioEngine::kAudioRate)
         // Oboe convertit lui-meme si le materiel impose autre chose : notre
         // reechantillonneur reste donc en court-circuit sur Android.
         ->setFormatConversionAllowed(true)
         ->setChannelConversionAllowed(true)
         ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::High)
         ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
         ->setSharingMode(oboe::SharingMode::Shared)
         ->setDataCallback(this)
         ->setErrorCallback(this);

        if (framesPerBuffer > 0) b.setFramesPerDataCallback(framesPerBuffer);
        // 0 vaut kUnspecified : Oboe suit alors le routage du systeme.
        if (deviceId > 0) b.setDeviceId(deviceId);

        if (dir == oboe::Direction::Input) {
            // VoiceRecognition desactive l'annulation d'echo et la correction
            // automatique de gain : la voix arrive telle quelle, et c'est notre
            // propre chaine de mise en forme qui s'en charge.
            b.setInputPreset(oboe::InputPreset::VoiceRecognition);
        } else {
            b.setContentType(oboe::ContentType::Speech);
            b.setUsage(oboe::Usage::Media);
        }

        std::shared_ptr<oboe::AudioStream> stream;
        const oboe::Result r = b.openStream(stream);
        if (r != oboe::Result::OK) {
            if (errorOut) *errorOut = QString::fromLatin1(oboe::convertToText(r));
            return false;
        }

        const oboe::Result started = stream->requestStart();
        if (started != oboe::Result::OK) {
            stream->close();
            if (errorOut) *errorOut = QString::fromLatin1(oboe::convertToText(started));
            return false;
        }

        if (dir == oboe::Direction::Input) input = stream; else output = stream;
        return true;
    }

    void close(oboe::Direction dir)
    {
        auto &s = (dir == oboe::Direction::Input) ? input : output;
        if (!s) return;
        s->requestStop();
        s->close();
        s.reset();
    }
};

// ----------------------------------------------------------- cycle de vie
bool AudioEngine::initialiseLibrary() { return true; }   // rien a initialiser
void AudioEngine::terminateLibrary() {}

AudioEngine::AudioEngine()
{
    m_impl = new OboeImpl;
    m_impl->owner = this;
}

AudioEngine::~AudioEngine()
{
    stopAll();
    delete m_impl;
    m_impl = nullptr;
}

// ------------------------------------------------------------ inventaire
QList<QPair<int, QString>> AudioEngine::hostApis()
{
    return {{0, QStringLiteral("Oboe")}};
}

int AudioEngine::defaultHostApi() { return 0; }
int AudioEngine::defaultInput()   { return kDefaultDeviceId; }
int AudioEngine::defaultOutput()  { return kDefaultDeviceId; }

static AudioDevice systemDevice(bool input)
{
    AudioDevice d;
    d.index = kDefaultDeviceId;
    d.name  = QCoreApplication::translate("AudioEngine", "System audio route");
    d.hostApi = QStringLiteral("Oboe");
    d.hostApiIndex = 0;
    d.maxIn  = input ? 1 : 0;
    d.maxOut = input ? 0 : 1;
    d.defaultSampleRate = AudioEngine::kAudioRate;
    return d;
}

// AudioDeviceInfo.TYPE_* : on ne garde que ce qui se branche sur un poste.
static QString deviceTypeLabel(int type)
{
    switch (type) {
    case 1:  return QCoreApplication::translate("AudioEngine", "built-in microphone");
    case 2:  return QCoreApplication::translate("AudioEngine", "earpiece");
    case 3:  return QCoreApplication::translate("AudioEngine", "wired headset");
    case 4:  return QCoreApplication::translate("AudioEngine", "wired headphones");
    case 7:  return QCoreApplication::translate("AudioEngine", "Bluetooth SCO");
    case 8:  return QCoreApplication::translate("AudioEngine", "Bluetooth A2DP");
    case 11: return QCoreApplication::translate("AudioEngine", "USB device");
    case 12: return QCoreApplication::translate("AudioEngine", "USB accessory");
    case 13: return QCoreApplication::translate("AudioEngine", "USB headset");
    case 15: return QCoreApplication::translate("AudioEngine", "built-in microphone");
    case 22: return QCoreApplication::translate("AudioEngine", "USB headset");
    default: return QCoreApplication::translate("AudioEngine", "audio device");
    }
}

// Android n'expose pas de liste a la PortAudio : il faut interroger
// AudioManager. L'identifiant retourne est celui qu'Oboe attend dans
// setDeviceId, ce qui permet de viser une carte USB precise.
static QList<AudioDevice> enumerateAndroid(bool input)
{
    QList<AudioDevice> list;
    list << systemDevice(input);

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return list;

    QJniObject service = QJniObject::fromString(QStringLiteral("audio"));
    QJniObject manager = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", service.object());
    if (!manager.isValid()) return list;

    // GET_DEVICES_INPUTS = 1, GET_DEVICES_OUTPUTS = 2
    const jint flags = input ? 1 : 2;
    QJniObject devices = manager.callObjectMethod(
        "getDevices", "(I)[Landroid/media/AudioDeviceInfo;", flags);
    if (!devices.isValid()) return list;

    QJniEnvironment env;
    auto array = devices.object<jobjectArray>();
    const jsize count = env->GetArrayLength(array);

    // Android decrit souvent le meme materiel plusieurs fois, une entree par
    // format ou par position de micro. On ne garde qu'un libelle de chaque.
    QSet<QString> seen;

    for (jsize i = 0; i < count; ++i) {
        QJniObject info(env->GetObjectArrayElement(array, i));
        if (!info.isValid()) continue;

        const int id   = int(info.callMethod<jint>("getId", "()I"));
        const int type = int(info.callMethod<jint>("getType", "()I"));

        QJniObject product = info.callObjectMethod(
            "getProductName", "()Ljava/lang/CharSequence;");
        QString name = product.isValid()
            ? product.callObjectMethod("toString", "()Ljava/lang/String;").toString()
            : QString();
        name = name.simplified();

        const QString label = name.isEmpty()
            ? deviceTypeLabel(type)
            : QStringLiteral("%1 — %2").arg(name, deviceTypeLabel(type));
        if (seen.contains(label)) continue;
        seen.insert(label);

        AudioDevice d;
        d.index = id;
        d.name  = label;
        d.hostApi = QStringLiteral("Oboe");
        d.hostApiIndex = 0;
        d.maxIn  = input ? 1 : 0;
        d.maxOut = input ? 0 : 1;
        d.defaultSampleRate = AudioEngine::kAudioRate;
        list << d;
    }
    return list;
}

QList<AudioDevice> AudioEngine::inputDevices(int)  { return enumerateAndroid(true);  }
QList<AudioDevice> AudioEngine::outputDevices(int) { return enumerateAndroid(false); }

// Oboe accepte toujours notre demande : il convertit lui-meme au besoin.
double AudioEngine::probeRate(int deviceIndex, bool)
{
    return deviceIndex < 0 ? 0.0 : double(kAudioRate);
}

void AudioEngine::clearProbeCache() {}

QString AudioEngine::describeDevices()
{
    return QStringLiteral("Oboe %1\nAudio route: chosen by the system\n"
                          "Rate delivered to the application: %2 Hz mono\n")
        .arg(QString::fromLatin1(oboe::Version::Text))
        .arg(kAudioRate);
}

// ---------------------------------------------------------------- flux
bool AudioEngine::startCapture(int deviceIndex, int framesPerBuffer)
{
    if (deviceIndex < 0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "PortAudio unavailable");
        return false;
    }
    stopCapture();

    m_inDevRate = kAudioRate;
    m_inResamp.init(kAudioRate, kAudioRate);      // court-circuit
    const size_t cap = size_t(framesPerBuffer > 0 ? framesPerBuffer : 1024) + 1024;
    m_inScratch.assign(cap, 0);
    m_inConverted.clear();
    m_inConverted.reserve(cap + 64);
    m_inRing.reset();

    m_impl->framesPerCallback = framesPerBuffer;
    m_impl->inputDeviceId = deviceIndex;
    QString err;
    if (!m_impl->open(oboe::Direction::Input, framesPerBuffer, deviceIndex, &err)) {
        m_lastError = err;
        return false;
    }
    m_inRunning = true;
    return true;
}

bool AudioEngine::startPlayback(int deviceIndex, int framesPerBuffer)
{
    if (deviceIndex < 0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "PortAudio unavailable");
        return false;
    }
    stopPlayback();

    m_outDevRate = kAudioRate;
    m_outResamp.init(kAudioRate, kAudioRate);     // court-circuit
    const size_t cap = size_t(framesPerBuffer > 0 ? framesPerBuffer : 1024) + 1024;
    m_outPulled.assign(cap + 64, 0);
    m_outFifo.clear();
    m_outFifo.reserve(cap * 2 + 8192);
    m_outFifoPos = 0;
    m_outRing.reset();

    m_impl->framesPerCallback = framesPerBuffer;
    m_impl->outputDeviceId = deviceIndex;
    QString err;
    if (!m_impl->open(oboe::Direction::Output, framesPerBuffer, deviceIndex, &err)) {
        m_lastError = err;
        return false;
    }
    m_outRunning = true;
    return true;
}

void AudioEngine::stopCapture()
{
    m_inRunning = false;
    if (m_impl) m_impl->close(oboe::Direction::Input);
}

void AudioEngine::stopPlayback()
{
    m_outRunning = false;
    if (m_impl) m_impl->close(oboe::Direction::Output);
    m_outFifo.clear();
    m_outFifoPos = 0;
}

void AudioEngine::stopAll() { stopCapture(); stopPlayback(); }

} // namespace rr

#endif // RR_AUDIO_OBOE
