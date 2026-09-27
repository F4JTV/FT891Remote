#include "serverwindow.h"

#include "../common/audioengine.h"
#include "../common/ft891.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#ifndef RR_VERSION
#define RR_VERSION "0.0.0"
#endif

namespace rr {

namespace {

// On an empty list currentData() is invalid and toInt() gives 0: device 0
// would be opened, which is not the one wanted.
int deviceIndexOf(const QComboBox *box)
{
    if (!box) return -1;
    const QVariant v = box->currentData();
    return v.isValid() ? v.toInt() : -1;
}

QWidget *row(std::initializer_list<QWidget *> widgets, int stretchIndex = 0)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    int i = 0;
    for (QWidget *x : widgets) l->addWidget(x, i++ == stretchIndex ? 1 : 0);
    return w;
}

// Explanations under the fields. They keep the theme's text colour: a
// fixed grey was readable in a light theme and nearly invisible in a dark one.
QLabel *hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

const char *kLedRx = "background:#204020;color:#8f8;font-weight:bold;padding:6px;border-radius:4px;";
const char *kLedTx = "background:#802020;color:#fdd;font-weight:bold;padding:6px;border-radius:4px;";
const char *kLedOff = "background:#303030;color:#888;font-weight:bold;padding:6px;border-radius:4px;";

} // namespace

ServerWindow::ServerWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("FT891Remote — Station server"));
    resize(820, 720);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QAction *about = help->addAction(tr("&About FT891Remote Server"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &ServerWindow::showAbout);

    m_station = new Station(this);
    connect(m_station, &Station::logMessage,    this, &ServerWindow::appendLog);
    connect(m_station, &Station::started,       this, &ServerWindow::onServerStarted);
    connect(m_station, &Station::clientChanged, this, &ServerWindow::onClientChanged);
    connect(m_station, &Station::statsUpdated,  this, &ServerWindow::onStats);
    connect(m_station, &Station::radioState,    this, &ServerWindow::onRadioState);
    connect(m_station, &Station::catReply, this, [this](const QString &reply) {
        if (!m_waitingCatReply) return;
        m_waitingCatReply = false;
        appendLog(tr("CAT reply: %1").arg(reply.isEmpty() ? tr("(none)") : reply));
    });

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildRadioPage(),   tr("Radio"));
    tabs->addTab(buildAudioPage(),   tr("Audio"));
    tabs->addTab(buildNetworkPage(), tr("Network"));
    tabs->addTab(buildCwPage(),      tr("CW"));
    root->addWidget(tabs);
    root->addWidget(buildStatusBox());

    m_startBtn = new QPushButton(tr("Start server"));
    m_startBtn->setMinimumHeight(38);
    connect(m_startBtn, &QPushButton::clicked, this, &ServerWindow::onStartStop);
    root->addWidget(m_startBtn);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    root->addWidget(m_log, 1);

    setCentralWidget(central);

    AudioEngine::initialiseLibrary();
    refreshDevices();
    loadSettings();
    onPttMethodChanged();
    refreshLocalAddresses();
    connect(m_tcpPort, &QSpinBox::valueChanged, this, &ServerWindow::refreshLocalAddresses);
    onRadioState(Ft891State());
}

ServerWindow::~ServerWindow()
{
    delete m_station;   // stops the station and joins its threads
    m_station = nullptr;
    AudioEngine::terminateLibrary();
}

void ServerWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    e->accept();
}

// ================================================================== pages
QWidget *ServerWindow::buildRadioPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_catPort = new QComboBox;
    m_catPort->setEditable(true);
    m_catPort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_catPort->setToolTip(tr("Pick from the list, or type a port name or path"));
    auto *rescanPorts = new QPushButton(tr("Rescan"));
    rescanPorts->setToolTip(tr("Look for serial ports again, after plugging in the radio"));
    connect(rescanPorts, &QPushButton::clicked, this, &ServerWindow::refreshSerialPorts);
    f->addRow(tr("CAT port"), row({m_catPort, rescanPorts}));
    f->addRow(QString(), hint(tr(
        "The FT-891's USB port shows two serial ports. CAT goes through the "
        "Enhanced one (the first, ttyUSB0 on Linux); the Standard one can carry "
        "RTS/DTR PTT or keying.")));

    m_catBaud = new QComboBox;
    m_catBaud->addItems({QStringLiteral("4800"), QStringLiteral("9600"),
                         QStringLiteral("19200"), QStringLiteral("38400")});
    m_catBaud->setCurrentText(QStringLiteral("38400"));
    f->addRow(tr("Speed"), m_catBaud);
    f->addRow(QString(), hint(tr("Must match menu 05-06 CAT RATE on the radio.")));

    m_pttMethod = new QComboBox;
    m_pttMethod->addItem(tr("CAT — TX1; / TX0;"), int(Ft891Config::PttCat));
    m_pttMethod->addItem(tr("RTS line"),          int(Ft891Config::PttRts));
    m_pttMethod->addItem(tr("DTR line"),          int(Ft891Config::PttDtr));
    m_pttMethod->addItem(tr("None — VOX or external"), int(Ft891Config::PttNone));
    connect(m_pttMethod, &QComboBox::currentIndexChanged, this, &ServerWindow::onPttMethodChanged);
    f->addRow(tr("PTT"), m_pttMethod);

    m_pttPort = new QComboBox;
    m_pttPort->setEditable(true);
    m_pttPort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_pttPortRow = m_pttPort;
    f->addRow(tr("PTT port"), m_pttPort);
    m_pttPort->setToolTip(tr("Usually the Standard COM port of the FT-891, with menus "
                             "11-08 SSB, 06-07 AM, 08-10 DATA and 09-03 PKT PTT SELECT "
                             "set to the same line. Empty: the CAT port itself, and menu "
                             "05-08 CAT RTS must then be DISABLE."));

    m_pollMs = new QSpinBox;
    m_pollMs->setRange(50, 2000);
    m_pollMs->setValue(200);
    m_pollMs->setSuffix(tr(" ms"));
    m_pollMs->setToolTip(tr("Frequency, mode, meters and one front-panel setting are "
                            "read at every cycle. 200 ms suits 38400 baud."));
    f->addRow(tr("Poll cycle"), m_pollMs);

    m_readTimeout = new QSpinBox;
    m_readTimeout->setRange(100, 3000);
    m_readTimeout->setValue(400);
    m_readTimeout->setSuffix(tr(" ms"));
    f->addRow(tr("Answer timeout"), m_readTimeout);

    m_powerOn = new QCheckBox(tr("Switch the radio on when the server starts"));
    f->addRow(QString(), m_powerOn);
    m_logTraffic = new QCheckBox(tr("Log every CAT frame (diagnostics)"));
    f->addRow(QString(), m_logTraffic);

    // No port scan here: the CW page, with its Key port list, is built after
    // this one. The constructor scans once every page exists.
    return w;
}

QWidget *ServerWindow::buildAudioPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_hostApi = new QComboBox;
    for (const auto &h : AudioEngine::hostApis()) m_hostApi->addItem(h.second, h.first);
    {
        const int def = m_hostApi->findData(AudioEngine::defaultHostApi());
        if (def >= 0) m_hostApi->setCurrentIndex(def);
    }
    connect(m_hostApi, &QComboBox::currentIndexChanged, this, [this] {
        refreshDevices();
        updateRateLabel();
    });
    f->addRow(tr("Audio interface"), m_hostApi);

    m_inDev = new QComboBox;
    f->addRow(tr("Input (audio from the radio)"), m_inDev);
    m_outDev = new QComboBox;
    f->addRow(tr("Output (audio to the radio)"), m_outDev);
    f->addRow(QString(), hint(tr(
        "The FT-891's USB port also carries a sound card (USB Audio CODEC). For "
        "the modulation to come from it, set 11-05 SSB MIC SELECT, 06-05 AM MIC "
        "SELECT, 09-01 FM MIC SELECT and 08-09 DATA IN SELECT to REAR — from the "
        "client's MENU page if you like.")));

    m_rateLabel = new QLabel(QStringLiteral("—"));
    m_rateLabel->setWordWrap(true);
    f->addRow(tr("Negotiated rate"), m_rateLabel);

    auto *rescan = new QPushButton(tr("Look for devices again"));
    rescan->setToolTip(tr("Needed after plugging in a USB sound card: "
                          "the device list is read once at startup."));
    connect(rescan, &QPushButton::clicked, this, &ServerWindow::onRescanDevices);
    f->addRow(QString(), rescan);
    connect(m_inDev,  &QComboBox::currentIndexChanged, this, &ServerWindow::updateRateLabel);
    connect(m_outDev, &QComboBox::currentIndexChanged, this, &ServerWindow::updateRateLabel);

    m_frames = new QComboBox;
    m_frames->addItem(tr("120 samples — 2.5 ms"), 120);
    m_frames->addItem(tr("240 samples — 5 ms"),   240);
    m_frames->addItem(tr("480 samples — 10 ms"),  480);
    m_frames->addItem(tr("960 samples — 20 ms"),  960);
    m_frames->setCurrentIndex(2);
    f->addRow(tr("Sound card buffer"), m_frames);

    m_rxGain = new QDoubleSpinBox;
    m_rxGain->setRange(0.1, 8.0); m_rxGain->setSingleStep(0.1); m_rxGain->setValue(1.0);
    f->addRow(tr("RX gain"), m_rxGain);
    m_txGain = new QDoubleSpinBox;
    m_txGain->setRange(0.1, 8.0); m_txGain->setSingleStep(0.1); m_txGain->setValue(1.0);
    f->addRow(tr("TX gain"), m_txGain);
    const auto pushGains = [this] {
        m_station->setGains(float(m_rxGain->value()), float(m_txGain->value()));
    };
    connect(m_rxGain, &QDoubleSpinBox::valueChanged, this, pushGains);
    connect(m_txGain, &QDoubleSpinBox::valueChanged, this, pushGains);

    m_tailMs = new QSpinBox;
    m_tailMs->setRange(0, 800); m_tailMs->setValue(120); m_tailMs->setSuffix(tr(" ms"));
    f->addRow(tr("PTT hold after transmit"), m_tailMs);


    m_rxMeter = new QProgressBar; m_rxMeter->setRange(0, 100); m_rxMeter->setTextVisible(false);
    m_txMeter = new QProgressBar; m_txMeter->setRange(0, 100); m_txMeter->setTextVisible(false);
    f->addRow(tr("RX level"), m_rxMeter);
    f->addRow(tr("TX level"), m_txMeter);
    return w;
}

QWidget *ServerWindow::buildNetworkPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_tcpPort = new QSpinBox; m_tcpPort->setRange(1, 65535); m_tcpPort->setValue(7300);
    f->addRow(tr("Control port (TCP)"), m_tcpPort);
    m_udpPort = new QSpinBox; m_udpPort->setRange(1, 65535); m_udpPort->setValue(7301);
    f->addRow(tr("Audio port (UDP)"), m_udpPort);

    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    f->addRow(tr("Password"), m_password);

    m_forceEnc = new QCheckBox(tr("Reject clients that do not encrypt"));
    f->addRow(QString(), m_forceEnc);

    m_bandEdges = new QCheckBox(tr("Refuse transmission out of the amateur bands"));
    m_bandEdges->setChecked(true);
    m_bandEdges->setToolTip(tr("Blocks the PTT, the tuner and the keyer when the emitted "
                               "spectrum would leave an amateur band. Turn this off for "
                               "a transverter or a licence with other allocations."));
    m_region = new QComboBox;
    m_region->addItem(tr("IARU Region 1 — Europe, Africa"), 1);
    m_region->addItem(tr("IARU Region 2 — Americas"), 2);
    m_region->addItem(tr("IARU Region 3 — Asia, Pacific"), 3);
    f->addRow(QString(), row({m_bandEdges, m_region}, 1));


    m_addrLabel = new QLabel;
    m_addrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_addrLabel->setWordWrap(true);
    m_addrLabel->setStyleSheet(QStringLiteral("font-family:monospace;"));
    f->addRow(tr("This machine"), m_addrLabel);

    auto *refresh = new QPushButton(tr("Refresh addresses"));
    connect(refresh, &QPushButton::clicked, this, &ServerWindow::refreshLocalAddresses);
    f->addRow(QString(), refresh);

    f->addRow(hint(tr(
        "On a local network or a VPN you can leave encryption off. Exposed to the "
        "Internet, turn it on and forward both ports to this machine.")));
    return w;
}

QWidget *ServerWindow::buildCwPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    f->addRow(hint(tr(
        "By default, text typed on the client is sent by the radio's own keyer: "
        "the server writes it into keyer memory 1 and plays it, fifty characters "
        "at a time. The radio must be in CW with KEYER on and BK-IN on.\n"
        "Alternatively the server can generate the elements itself and key a "
        "serial line wired to the KEY jack.")));

    m_cwEnable = new QCheckBox(tr("Generate CW here and key a serial line"));
    f->addRow(QString(), m_cwEnable);

    m_cwPort = new QComboBox;
    m_cwPort->setEditable(true);
    m_cwPort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cwLine = new QComboBox;
    m_cwLine->addItems({QStringLiteral("DTR"), QStringLiteral("RTS")});
    auto *rescanKey = new QPushButton(tr("Rescan"));
    rescanKey->setToolTip(tr("Look for serial ports again"));
    connect(rescanKey, &QPushButton::clicked, this, &ServerWindow::refreshSerialPorts);
    auto *portRow = row({m_cwPort, m_cwLine, rescanKey});
    f->addRow(tr("Key port"), portRow);

    m_cwInvert = new QCheckBox(tr("Invert the line"));
    m_cwHoldPtt = new QCheckBox(tr("Hold PTT during the message"));
    m_cwCorr = new QSpinBox;
    m_cwCorr->setRange(0, 30);
    m_cwCorr->setSuffix(tr(" ms"));
    m_cwCorr->setToolTip(tr("Shortens every keyed element, never the silences, to "
                            "make up for the time the radio takes to raise its carrier."));
    auto *opts = row({m_cwInvert, m_cwHoldPtt, new QLabel(tr("Correction")), m_cwCorr}, 99);
    f->addRow(QString(), opts);

    const auto toggle = [portRow, opts](bool on) {
        portRow->setEnabled(on);
        opts->setEnabled(on);
    };
    connect(m_cwEnable, &QCheckBox::toggled, this, toggle);
    toggle(false);
    return w;
}

QWidget *ServerWindow::buildStatusBox()
{
    auto *box = new QGroupBox(tr("Status"));
    auto *v = new QVBoxLayout(box);

    auto *top = new QHBoxLayout;
    m_txLed = new QLabel(QStringLiteral("RX"));
    m_txLed->setAlignment(Qt::AlignCenter);
    m_txLed->setMinimumWidth(64);
    m_freqLabel = new QLabel(QStringLiteral("—"));
    QFont ff = m_freqLabel->font();
    ff.setPointSize(ff.pointSize() + 5);
    ff.setBold(true);
    ff.setFamily(QStringLiteral("monospace"));
    m_freqLabel->setFont(ff);
    m_sMeter = new QProgressBar;
    m_sMeter->setRange(0, 255);
    m_sMeter->setFormat(QStringLiteral("%v"));
    m_sMeter->setMaximumWidth(220);
    top->addWidget(m_txLed);
    top->addWidget(m_freqLabel, 1);
    top->addWidget(m_sMeter);
    v->addLayout(top);

    m_radioLabel  = new QLabel(QStringLiteral("—"));
    m_statusLabel = new QLabel(tr("Stopped"));
    m_clientLabel = new QLabel(tr("No client"));
    auto *mid = new QHBoxLayout;
    mid->addWidget(m_statusLabel, 1);
    mid->addWidget(m_radioLabel, 2);
    mid->addWidget(m_clientLabel, 2);
    v->addLayout(mid);

    m_catLine = new QLineEdit;
    m_catLine->setPlaceholderText(tr("CAT frame, e.g. ID; or EX0506;"));
    auto *send = new QPushButton(tr("Send"));
    connect(send, &QPushButton::clicked, this, &ServerWindow::onSendCat);
    connect(m_catLine, &QLineEdit::returnPressed, this, &ServerWindow::onSendCat);
    v->addWidget(row({new QLabel(tr("CAT test")), m_catLine, send}, 1));
    return box;
}

// ================================================================== logic
// A port given as a path rather than a name: /dev/serial/by-id/… on Linux,
// or the pseudo-terminal of a simulator. The system does not list these.
static bool pathExists(const QString &port)
{
    return port.startsWith(QLatin1Char('/')) && QFileInfo::exists(port);
}

static QList<QPair<QString, QString>> localIPv4Addresses()
{
    QList<QPair<QString, QString>> out;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) && !(flags & QNetworkInterface::IsRunning))
            continue;
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (a.isLoopback()) continue;
            if (a.toString().startsWith(QLatin1String("169.254."))) continue;
            out.append({a.toString(), iface.humanReadableName()});
        }
    }
    return out;
}

void ServerWindow::refreshLocalAddresses()
{
    if (!m_addrLabel) return;
    QStringList lines;
    for (const auto &a : localIPv4Addresses())
        lines << QStringLiteral("%1:%2   (%3)").arg(a.first).arg(m_tcpPort->value()).arg(a.second);
    m_addrLabel->setText(lines.isEmpty()
        ? tr("No network address found — is this machine connected?")
        : lines.join(QLatin1Char('\n')));
}

void ServerWindow::refreshSerialPorts()
{
    // Rescan: the lists keep what is selected in them.
    applySerialPorts(portOf(m_catPort), portOf(m_pttPort), portOf(m_cwPort));
}

void ServerWindow::applySerialPorts(const QString &cat, const QString &ptt, const QString &key)
{
    m_ports = scanSerialPorts(m_ft891Id);

    auto find = [this](PortKind kind) -> const SerialPortEntry * {
        for (const SerialPortEntry &p : m_ports)
            if (p.kind == kind) return &p;
        return nullptr;
    };
    auto present = [this](const QString &name) {
        for (const SerialPortEntry &p : m_ports)
            if (p.name == name) return true;
        return pathExists(name);
    };
    // Only the FT-891 this server has already talked to. A CP2105 that has
    // not answered ID0650 may be an SCU-17, whose Standard port keys another
    // radio through its RTS and DTR lines: it is never chosen on its own.
    const SerialPortEntry *enhanced = find(PortKind::Ft891Enhanced);
    const SerialPortEntry *standard = find(PortKind::Ft891Standard);

    // CAT: the saved port if it is there. If not, the known FT-891's Enhanced
    // port: Windows gives the radio another COM number when it is plugged
    // into another USB socket.
    QString catWanted = cat;
    if (!present(cat) && enhanced) {
        if (!cat.isEmpty())
            appendLog(tr("CAT port %1 not found; the FT-891 is now on %2.").arg(cat, enhanced->name));
        catWanted = enhanced->name;
    }
    // Key port: nothing chosen yet, the known FT-891's Standard port. PTT
    // port: empty means the CAT port itself, which is kept.
    const QString keyWanted = key.isEmpty() && standard ? standard->name : key;

    fillPortCombo(m_catPort, catWanted, false);
    fillPortCombo(m_pttPort, ptt, true);
    fillPortCombo(m_cwPort, keyWanted, false);

    QStringList others;
    for (const SerialPortEntry &p : m_ports)
        if (p.kind == PortKind::Cp2105Enhanced || p.kind == PortKind::Cp2105Standard) others << p.name;

    if (enhanced || standard) {
        QStringList found;
        if (enhanced) found << tr("Enhanced (CAT) on %1").arg(enhanced->name);
        if (standard) found << tr("Standard on %1").arg(standard->name);
        appendLog(tr("FT-891 found: %1.").arg(found.join(QStringLiteral(", "))));
        if ((enhanced && enhanced->guessed) || (standard && standard->guessed))
            appendLog(tr("Its two ports were told apart by their order only: check that "
                         "CAT answers on the one marked Enhanced."));
    }
    if (!others.isEmpty()) {
        appendLog(tr("CP2105 serial ports not yet identified: %1. The FT-891 and Yaesu's "
                     "SCU-17 use the same chip; choose the FT-891's Enhanced port for CAT, "
                     "and it will be recognised once it has answered.")
                      .arg(others.join(QStringLiteral(", "))));
    }
    if (!enhanced && !standard && others.isEmpty()) {
        appendLog(tr("No FT-891 among the serial ports (%n port(s) found). "
                     "Is its USB cable connected?", nullptr, int(m_ports.size())));
    }
}

void ServerWindow::learnFt891(const QString &portName)
{
    // Once per port: the state arrives several times a second.
    if (portName.isEmpty() || portName == m_identifiedPort) return;
    m_identifiedPort = portName;

    const QString id = ft891IdentityOf(scanSerialPorts(), portName);
    if (id.isEmpty() || id == m_ft891Id) return;   // not a CP2105, or known

    m_ft891Id = id;
    QSettings s(settingsOrganisation(), settingsApplication());
    s.setValue(QStringLiteral("ft891UsbIdentity"), id);
    appendLog(tr("The FT-891 answered on %1: its USB ports are now recognised (%2).")
                  .arg(portName, id));
    // The lists are labelled again; what is selected in them stays.
    refreshSerialPorts();
}

void ServerWindow::fillPortCombo(QComboBox *box, const QString &wanted, bool withCatPortItem)
{
    if (!box) return;
    const QSignalBlocker b(box);
    box->clear();

    // The label is shown, the port name is kept as the item's data.
    if (withCatPortItem) box->addItem(tr("(same as the CAT port)"), QString());
    bool separated = false;
    for (const SerialPortEntry &p : m_ports) {
        // Built-in and virtual ports after a line: USB comes first.
        if (p.kind == PortKind::Other && !separated && box->count() > 0) {
            box->insertSeparator(box->count());
            separated = true;
        }
        box->addItem(p.label, p.name);
    }

    int index = box->findData(wanted);
    if (index < 0 && !wanted.isEmpty()) {
        // A saved port that is not in the scan stays visible. A path that
        // exists — /dev/serial/by-id/…, a virtual port — is not listed by the
        // system but works; anything else is marked.
        box->addItem(pathExists(wanted) ? wanted : tr("%1 — not found").arg(wanted), wanted);
        index = box->count() - 1;
    }
    if (index < 0 && !withCatPortItem) {
        box->setCurrentIndex(-1);
        box->setEditText(QString());
        return;
    }
    box->setCurrentIndex(qMax(0, index));
}

QString ServerWindow::portOf(const QComboBox *box) const
{
    if (!box) return {};
    const int i = box->currentIndex();
    // A chosen item gives its port name; text typed in the field, such as a
    // path to a virtual port, is taken as it is.
    if (i >= 0 && box->currentText() == box->itemText(i)) return box->itemData(i).toString();
    return portNameOf(box->currentText());
}

void ServerWindow::refreshDevices()
{
    const int api = m_hostApi ? m_hostApi->currentData().toInt() : -1;
    const QSignalBlocker b1(m_inDev), b2(m_outDev);

    m_inDev->clear();
    for (const auto &d : AudioEngine::inputDevices(api)) m_inDev->addItem(d.name, d.index);
    if (m_inDev->count() == 0) m_inDev->addItem(tr("No capture device found"), -1);

    m_outDev->clear();
    for (const auto &d : AudioEngine::outputDevices(api)) m_outDev->addItem(d.name, d.index);
    if (m_outDev->count() == 0) m_outDev->addItem(tr("No playback device found"), -1);
}

void ServerWindow::onRescanDevices()
{
    if (m_running) {
        appendLog(tr("Stop the server first: the device list cannot be reread "
                     "while the audio streams are open."));
        return;
    }
    AudioEngine::rescanDevices();
    const QString keptApi = m_hostApi->currentText();
    {
        const QSignalBlocker b(m_hostApi);
        m_hostApi->clear();
        for (const auto &h : AudioEngine::hostApis()) m_hostApi->addItem(h.second, h.first);
        const int i = m_hostApi->findText(keptApi);
        if (i >= 0) m_hostApi->setCurrentIndex(i);
    }
    refreshDevices();
    updateRateLabel();
    appendLog(tr("Device list reread."));
}

void ServerWindow::updateRateLabel()
{
    if (!m_rateLabel) return;
    const int inIdx  = deviceIndexOf(m_inDev);
    const int outIdx = deviceIndexOf(m_outDev);
    const double in  = inIdx  < 0 ? -1.0 : AudioEngine::probeRate(inIdx, true);
    const double out = outIdx < 0 ? -1.0 : AudioEngine::probeRate(outIdx, false);

    auto describe = [](double r) {
        if (r < 0.0)  return tr("no device");
        if (r == 0.0) return tr("rejected");
        if (int(r) == AudioEngine::kAudioRate) return tr("48000 Hz, direct");
        return tr("%1 Hz, resampled").arg(int(r));
    };
    m_rateLabel->setText(tr("Input: %1  ·  Output: %2").arg(describe(in), describe(out)));
}

void ServerWindow::onPttMethodChanged()
{
    const auto m = Ft891Config::PttMethod(m_pttMethod->currentData().toInt());
    m_pttPort->setEnabled(m == Ft891Config::PttRts || m == Ft891Config::PttDtr);
}

StationConfig ServerWindow::configFromUi() const
{
    StationConfig c;
    Ft891Config &r = c.radio;
    r.catPort        = portOf(m_catPort);
    r.catBaud        = m_catBaud->currentText().toInt();
    r.ptt            = Ft891Config::PttMethod(m_pttMethod->currentData().toInt());
    r.pttPort        = portOf(m_pttPort);
    r.pollMs         = m_pollMs->value();
    r.readTimeoutMs  = m_readTimeout->value();
    r.powerOnAtStart = m_powerOn->isChecked();
    r.logTraffic     = m_logTraffic->isChecked();

    ServerConfig &n = c.server;
    n.tcpPort   = quint16(m_tcpPort->value());
    n.udpPort   = quint16(m_udpPort->value());
    n.password  = m_password->text();
    n.requireEncryption = m_forceEnc->isChecked();
    n.framesPerBuffer   = m_frames->currentData().toInt();
    n.rxGain    = float(m_rxGain->value());
    n.txGain    = float(m_txGain->value());
    n.pttTailMs = m_tailMs->value();
    n.enforceBandEdges = m_bandEdges->isChecked();
    n.region    = m_region->currentData().toInt();

    CwKeyerConfig &k = c.keyer;
    k.enabled      = m_cwEnable->isChecked();
    k.port         = portOf(m_cwPort);
    k.line         = m_cwLine->currentText();
    k.inverted     = m_cwInvert->isChecked();
    k.correctionMs = m_cwCorr->value();
    k.holdPtt      = m_cwHoldPtt->isChecked();

    // Kept by name: an index changes whenever a device is plugged in.
    c.audioIn  = deviceIndexOf(m_inDev)  >= 0 ? m_inDev->currentText()  : QString();
    c.audioOut = deviceIndexOf(m_outDev) >= 0 ? m_outDev->currentText() : QString();
    c.hostApi  = m_hostApi->currentText();
    return c;
}

void ServerWindow::onStartStop()
{
    if (m_running) {
        m_station->stop();
        setRunning(false);
        m_statusLabel->setText(tr("Stopped"));
        onRadioState(Ft891State());
        return;
    }

    if (deviceIndexOf(m_inDev) < 0 || deviceIndexOf(m_outDev) < 0) {
        appendLog(tr("The station needs one capture device and one playback device."));
        return;
    }
    const StationConfig cfg = configFromUi();
    if (cfg.radio.catPort.isEmpty()) {
        appendLog(tr("Choose the CAT port of the FT-891 first."));
        return;
    }
    if (cfg.server.password.isEmpty())
        appendLog(tr("Warning: empty password — anyone reaching the port can key the radio."));
    saveSettings();

    QString err;
    if (!m_station->start(cfg, &err)) {
        appendLog(err);
        return;
    }
    m_statusLabel->setText(tr("Starting…"));
}

void ServerWindow::onSendCat()
{
    const QString t = m_catLine->text().trimmed();
    if (t.isEmpty()) return;
    if (!m_running) {
        appendLog(tr("Start the server first: it owns the CAT port."));
        return;
    }
    // A frame the description lists as a read, or two bare letters, waits
    // for an answer; anything else is a setting, which the radio accepts in
    // silence.
    QString bare = t.toUpper();
    while (bare.endsWith(QLatin1Char(';'))) bare.chop(1);
    bool isRead = bare.size() == 2;
    for (const CatGroup &g : ft891::description().groups)
        for (const CatCommand &c : g.commands) {
            QString r = c.readTemplate;
            while (r.endsWith(QLatin1Char(';'))) r.chop(1);
            if (!r.isEmpty() && r == bare) isRead = true;
        }
    m_waitingCatReply = isRead;
    appendLog(tr("CAT test: %1").arg(t));
    m_station->sendCat(t, isRead);
}

void ServerWindow::setRunning(bool running)
{
    m_running = running;
    m_startBtn->setText(running ? tr("Stop server") : tr("Start server"));
}

void ServerWindow::onServerStarted(bool ok, const QString &msg)
{
    appendLog(msg);
    setRunning(ok);
    m_statusLabel->setText(ok ? msg : tr("Start refused"));
    if (!ok) m_station->stop();
}

void ServerWindow::onRadioState(const Ft891State &st)
{
    // ID0650 on the CAT port: that CP2105 is the FT-891, not an SCU-17.
    if (st.linkOpen && st.radioOn && st.radioId == QLatin1String("0650")) learnFt891(st.portName);

    if (!st.linkOpen) {
        m_radioLabel->setText(st.error.isEmpty() ? QStringLiteral("—") : st.error);
        m_freqLabel->setText(QStringLiteral("—"));
        m_txLed->setText(QStringLiteral("OFF"));
        m_txLed->setStyleSheet(QLatin1String(kLedOff));
        m_sMeter->setValue(0);
        return;
    }
    if (!st.radioOn) {
        m_radioLabel->setText(tr("%1 open — the radio does not answer").arg(st.portName));
        m_freqLabel->setText(QStringLiteral("—"));
        m_txLed->setText(QStringLiteral("OFF"));
        m_txLed->setStyleSheet(QLatin1String(kLedOff));
        m_sMeter->setValue(0);
        return;
    }

    const QString f = QLocale::c().toString(double(st.freqA) / 1e6, 'f', 5);
    m_freqLabel->setText(QStringLiteral("%1 MHz  %2").arg(f, st.mode));
    QStringList bits;
    bits << QStringLiteral("FT-891") << st.portName << st.memMode;
    if (st.memMode != QLatin1String("VFO")) bits << QStringLiteral("CH %1").arg(st.memChannel);
    if (st.freqB) bits << QStringLiteral("B %1").arg(QLocale::c().toString(double(st.freqB) / 1e6, 'f', 5));
    if (!st.txAllowed) bits << tr("OUT OF BAND");
    if (st.tuning) bits << tr("TUNING");
    if (st.cw) bits << tr("KEYING");
    m_radioLabel->setText(bits.join(QStringLiteral(" · ")));

    m_txLed->setText(st.ptt ? QStringLiteral("TX") : QStringLiteral("RX"));
    m_txLed->setStyleSheet(QLatin1String(st.ptt ? kLedTx : kLedRx));
    m_sMeter->setValue(st.ptt ? st.po : st.sMeter);
    m_sMeter->setFormat(st.ptt ? QStringLiteral("PO %1 W").arg(ft891::powerWatts(st.po), 0, 'f', 0)
                               : ft891::sMeterText(st.sMeter));
}

void ServerWindow::onClientChanged(const QString &peer, bool connected, bool encrypted,
                                   const QString &codec)
{
    m_clientLabel->setText(connected
        ? tr("%1 · %2 · %3").arg(peer, codec, encrypted ? tr("encrypted") : tr("clear"))
        : tr("No client"));
}

void ServerWindow::onStats(int, int, float rxLevel, float txLevel)
{
    m_rxMeter->setValue(int(rxLevel * 100));
    m_txMeter->setValue(int(txLevel * 100));
}

void ServerWindow::appendLog(const QString &msg)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + msg);
}

void ServerWindow::showAbout()
{
    QDialog d(this);
    d.setWindowTitle(tr("About FT891Remote Server"));
    auto *v = new QVBoxLayout(&d);
    auto *top = new QHBoxLayout;
    auto *logo = new QLabel;
    const QPixmap pix(QStringLiteral(":/icons/ft891remote-server.png"));
    if (!pix.isNull()) logo->setPixmap(pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignTop);
    top->addWidget(logo);
    auto *text = new QLabel(tr(
        "<h2>FT891Remote Server</h2>"
        "<p>Version %1 · MIT licence</p>"
        "<p>Runs next to a Yaesu FT-891 and shares it over the network: two-way "
        "audio, PTT, and every setting of the radio through its own CAT commands, "
        "without Hamlib. Derived from RemoteRig.</p>").arg(QStringLiteral(RR_VERSION)));
    text->setWordWrap(true);
    text->setMinimumWidth(360);
    top->addWidget(text, 1);
    v->addLayout(top);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.exec();
}

// =============================================================== settings
void ServerWindow::loadSettings()
{
    QSettings s(settingsOrganisation(), settingsApplication());
    const StationConfig c = loadStationConfig(s);

    const int api = m_hostApi->findText(c.hostApi);
    if (api >= 0) { m_hostApi->setCurrentIndex(api); refreshDevices(); }

    m_catBaud->setCurrentText(QString::number(c.radio.catBaud));
    m_pttMethod->setCurrentIndex(qMax(0, m_pttMethod->findData(int(c.radio.ptt))));
    m_pollMs->setValue(c.radio.pollMs);
    m_readTimeout->setValue(c.radio.readTimeoutMs);
    m_powerOn->setChecked(c.radio.powerOnAtStart);
    m_logTraffic->setChecked(c.radio.logTraffic);

    m_tcpPort->setValue(c.server.tcpPort);
    m_udpPort->setValue(c.server.udpPort);
    m_password->setText(c.server.password);
    m_forceEnc->setChecked(c.server.requireEncryption);
    m_frames->setCurrentIndex(qMax(0, m_frames->findData(c.server.framesPerBuffer)));
    m_rxGain->setValue(c.server.rxGain);
    m_txGain->setValue(c.server.txGain);
    m_tailMs->setValue(c.server.pttTailMs);
    m_bandEdges->setChecked(c.server.enforceBandEdges);
    m_region->setCurrentIndex(qMax(0, m_region->findData(c.server.region)));

    m_cwEnable->setChecked(c.keyer.enabled);
    m_cwLine->setCurrentText(c.keyer.line);
    m_cwInvert->setChecked(c.keyer.inverted);
    m_cwCorr->setValue(c.keyer.correctionMs);
    m_cwHoldPtt->setChecked(c.keyer.holdPtt);

    // The serial ports are scanned here, once every page exists, and each list
    // selects its saved port by name.
    m_ft891Id = s.value(QStringLiteral("ft891UsbIdentity")).toString();
    applySerialPorts(c.radio.catPort, c.radio.pttPort, c.keyer.port);

    const int in = m_inDev->findText(c.audioIn);
    if (in >= 0) m_inDev->setCurrentIndex(in);
    const int out = m_outDev->findText(c.audioOut);
    if (out >= 0) m_outDev->setCurrentIndex(out);
    updateRateLabel();
}

void ServerWindow::saveSettings()
{
    QSettings s(settingsOrganisation(), settingsApplication());
    saveStationConfig(s, configFromUi());
}

} // namespace rr
