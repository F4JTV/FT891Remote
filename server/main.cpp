// FT891Remote server: graphical by default, --headless for a station
// without a desktop.
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMetaType>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "../common/audioengine.h"
#include "../common/i18n.h"
#include "../common/protocol.h"
#include "cwkeyer.h"
#include "ft891controller.h"
#include "serialports.h"
#include "servercore.h"
#include "serverdaemon.h"
#include "serverwindow.h"
#include "station.h"

#include <csignal>

#ifndef RR_VERSION
#define RR_VERSION "0.0.0"
#endif

namespace {

void printUsage()
{
    QTextStream(stdout)
        << "FT891Remote server " RR_VERSION "\n\n"
        << "  ft891remote-server                    graphical interface\n"
        << "  ft891remote-server --headless         run without a desktop session\n"
        << "  ft891remote-server --headless -v      ... and log frequency changes\n"
        << "  ft891remote-server --config FILE      read an .ini instead of the saved settings\n"
        << "  ft891remote-server --list-audio       list audio devices and exit\n"
        << "  ft891remote-server --list-ports       list serial ports and exit\n"
        << "  ft891remote-server --help\n\n"
        << "Headless mode reuses the settings made in the graphical interface.\n"
        << "Configure once with a display, then run without one.\n";
}

bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], name) == 0) return true;
    return false;
}

QString argValue(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc - 1; ++i)
        if (qstrcmp(argv[i], name) == 0) return QString::fromLocal8Bit(argv[i + 1]);
    return QString();
}

extern "C" void onTerminationSignal(int)
{
    // A signal handler can do almost nothing safely: it raises a flag that
    // a timer reads back.
    rr::ServerDaemon::requestStop();
}

void registerTypes()
{
    qRegisterMetaType<rr::Ft891State>("rr::Ft891State");
    qRegisterMetaType<rr::Ft891Config>("rr::Ft891Config");
    qRegisterMetaType<rr::ServerConfig>("rr::ServerConfig");
    qRegisterMetaType<rr::CwKeyerConfig>("rr::CwKeyerConfig");
}

void identify(QCoreApplication &app)
{
    app.setApplicationName(rr::settingsApplication());
    app.setOrganizationName(rr::settingsOrganisation());
    app.setApplicationVersion(QStringLiteral(RR_VERSION));
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Logs and --list-audio are written in UTF-8: the console must read them
    // so, or an accented device name comes out garbled there too.
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        printUsage();
        return 0;
    }
    registerTypes();

    if (hasArg(argc, argv, "--list-audio")) {
        QCoreApplication probe(argc, argv);
        QTextStream(stdout) << rr::AudioEngine::describeDevices();
        rr::AudioEngine::terminateLibrary();
        return 0;
    }
    if (hasArg(argc, argv, "--list-ports")) {
        QCoreApplication probe(argc, argv);
        const QList<rr::SerialPortEntry> ports = rr::scanSerialPorts();
        for (const rr::SerialPortEntry &p : ports) QTextStream(stdout) << p.label << "\n";
        if (ports.isEmpty()) QTextStream(stdout) << "No serial port found.\n";
        return 0;
    }

    // ---------------------------------------------------------- headless
    if (hasArg(argc, argv, "--headless")) {
        QCoreApplication app(argc, argv);
        identify(app);
        rr::installTranslators(app, rr::readLanguage(rr::settingsApplication()));

        std::signal(SIGINT, onTerminationSignal);
        std::signal(SIGTERM, onTerminationSignal);

        rr::ServerDaemon daemon;
        const bool verbose = hasArg(argc, argv, "-v") || hasArg(argc, argv, "--verbose");
        if (!daemon.start(argValue(argc, argv, "--config"), verbose))
            return 1;

        const int code = app.exec();
        daemon.stop();
        rr::AudioEngine::terminateLibrary();
        return code;
    }

    // --------------------------------------------------------- graphical
    QApplication app(argc, argv);
    identify(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/ft891remote-server.png")));
    rr::installTranslators(app, rr::readLanguage(rr::settingsApplication()));

    rr::ServerWindow w;
    w.show();
    return app.exec();
}
