// Server window: configuration of the station, and a view of what the radio
// is doing while a remote operator uses it.
#pragma once

#include <QMainWindow>

#include "station.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace rr {

class ServerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ServerWindow(QWidget *parent = nullptr);
    ~ServerWindow() override;

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onStartStop();
    void onRadioState(const rr::Ft891State &st);
    void onServerStarted(bool ok, const QString &msg);
    void onClientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec);
    void onStats(int rttMs, int lost, float rxLevel, float txLevel);
    void appendLog(const QString &msg);
    void onRescanDevices();
    void refreshSerialPorts();
    void onPttMethodChanged();
    void onSendCat();

private:
    QWidget *buildRadioPage();
    QWidget *buildAudioPage();
    QWidget *buildNetworkPage();
    QWidget *buildCwPage();
    QWidget *buildStatusBox();
    void refreshDevices();
    // Fills the three port lists from a fresh scan. The names given are the
    // ports to select; a missing one stays shown, marked as not found.
    void applySerialPorts(const QString &cat, const QString &ptt, const QString &key);
    void fillPortCombo(QComboBox *box, const QString &wanted, bool withCatPortItem);
    QString portOf(const QComboBox *box) const;
    void refreshLocalAddresses();
    void updateRateLabel();
    void setRunning(bool running);
    void loadSettings();
    void saveSettings();
    StationConfig configFromUi() const;
    void showAbout();

    Station *m_station = nullptr;
    bool     m_running = false;
    QList<SerialPortEntry> m_ports;
    // How to recognise the FT-891 among CP2105 devices: learnt when it
    // answers ID0650, kept in the settings. See serialports.h.
    QString m_ft891Id;
    QString m_identifiedPort;       // port last checked, to scan only once
    void learnFt891(const QString &portName);

    // radio
    QComboBox *m_catPort  = nullptr;
    QComboBox *m_catBaud  = nullptr;
    QComboBox *m_pttMethod = nullptr;
    QComboBox *m_pttPort  = nullptr;
    QWidget   *m_pttPortRow = nullptr;
    QSpinBox  *m_pollMs   = nullptr;
    QSpinBox  *m_readTimeout = nullptr;
    QCheckBox *m_powerOn  = nullptr;
    QCheckBox *m_logTraffic = nullptr;

    // audio
    QComboBox *m_hostApi = nullptr;
    QLabel    *m_rateLabel = nullptr;
    QComboBox *m_inDev  = nullptr;
    QComboBox *m_outDev = nullptr;
    QComboBox *m_frames = nullptr;
    QDoubleSpinBox *m_rxGain = nullptr;
    QDoubleSpinBox *m_txGain = nullptr;
    QSpinBox  *m_tailMs = nullptr;
    QProgressBar *m_rxMeter = nullptr;
    QProgressBar *m_txMeter = nullptr;

    // network
    QSpinBox  *m_tcpPort = nullptr;
    QSpinBox  *m_udpPort = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_forceEnc = nullptr;
    QCheckBox *m_bandEdges = nullptr;
    QComboBox *m_region = nullptr;
    QLabel    *m_addrLabel = nullptr;

    // local keyer
    QCheckBox *m_cwEnable = nullptr;
    QComboBox *m_cwPort = nullptr;
    QComboBox *m_cwLine = nullptr;
    QCheckBox *m_cwInvert = nullptr;
    QSpinBox  *m_cwCorr = nullptr;
    QCheckBox *m_cwHoldPtt = nullptr;

    // status
    QPushButton *m_startBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_radioLabel = nullptr;
    QLabel *m_freqLabel = nullptr;
    QLabel *m_clientLabel = nullptr;
    QLabel *m_txLed = nullptr;
    QProgressBar *m_sMeter = nullptr;
    QLineEdit *m_catLine = nullptr;
    QPlainTextEdit *m_log = nullptr;
    bool m_waitingCatReply = false;
};

} // namespace rr
