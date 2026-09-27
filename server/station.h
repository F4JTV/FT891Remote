// The whole station: network core, FT-891 controller and local CW keyer, each
// in its own thread, wired together once.
//
// RemoteRig wired these three objects twice, in the window and in the
// headless daemon, and the two copies drifted apart. Here both front ends
// share this class and the settings code below.
#pragma once

#include <QObject>
#include <QThread>
#include <QVariantMap>

#include "cwkeyer.h"
#include "ft891controller.h"
#include "serialports.h"
#include "servercore.h"

class QSettings;

namespace rr {

struct StationConfig {
    Ft891Config   radio;
    ServerConfig  server;
    CwKeyerConfig keyer;
    // Audio devices are kept by name: an index changes whenever a USB
    // device is plugged in. They are resolved when the station starts.
    QString       audioIn;
    QString       audioOut;
    QString       hostApi;
};

// Organisation and application names of the saved settings.
QString settingsOrganisation();
QString settingsApplication();

StationConfig loadStationConfig(QSettings &s);
void saveStationConfig(QSettings &s, const StationConfig &c);

// Device index for a saved name; the system default for an empty name,
// -1 if the device is gone.
int audioDeviceByName(const QString &name, bool input, int hostApi = -1);

class Station : public QObject {
    Q_OBJECT
public:
    explicit Station(QObject *parent = nullptr);
    ~Station() override;

    // Opens the radio and starts serving. Returns false, with a reason, if
    // the audio devices cannot be found; the radio link retries on its own.
    bool start(const StationConfig &cfg, QString *error);
    void stop();
    bool isRunning() const { return m_running; }

    // For the server window: a frame typed on the station itself.
    void sendCat(const QString &frame, bool expectReply);
    void setGains(float rx, float tx);

signals:
    void logMessage(const QString &msg);
    void started(bool ok, const QString &message);
    void radioOpened(bool ok, const QString &message);
    void radioState(const rr::Ft891State &st);
    void radioValues(const QVariantMap &delta);
    void clientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec);
    void statsUpdated(int rttMs, int lostPackets, float rxLevel, float txLevel);
    void catReply(const QString &reply);

private:
    QThread          m_netThread;
    QThread          m_radioThread;
    QThread          m_keyerThread;
    ServerCore      *m_core  = nullptr;
    Ft891Controller *m_radio = nullptr;
    CwKeyer         *m_keyer = nullptr;
    bool             m_cwLocal = false;
    bool             m_running = false;
};

} // namespace rr
