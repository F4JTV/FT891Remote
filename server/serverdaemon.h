// Server without a graphical interface.
//
// A remote station has no reason to run a desktop. This mode reads the
// configuration made in the window (or an INI file), opens the radio and the
// streams, and logs to standard output, where systemd collects it.
#pragma once

#include <QObject>

#include "station.h"

namespace rr {

class ServerDaemon : public QObject {
    Q_OBJECT
public:
    explicit ServerDaemon(QObject *parent = nullptr);
    ~ServerDaemon() override;

    // Empty configPath: the settings saved by the window. Otherwise an INI
    // file, to run several stations on one machine.
    bool start(const QString &configPath, bool verbose);
    void stop();

    // The signal handler only raises this flag, which a timer reads back:
    // nothing else is safe to call from a signal.
    static void requestStop();

private slots:
    void onLog(const QString &message);
    void onStarted(bool ok, const QString &message);
    void onClientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec);
    void onRadioState(const rr::Ft891State &state);
    void checkStopRequest();

private:
    void report(const QString &line) const;
    void reportAddresses(quint16 tcpPort) const;

    Station *m_station = nullptr;
    bool     m_verbose = false;
    bool     m_lastPtt = false;
    bool     m_lastOn = false;
    QString  m_lastLine;
};

} // namespace rr
