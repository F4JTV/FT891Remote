// FT891Remote client: one Qt Quick front panel for the desktop and Android.
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <qqml.h>

#include "../../common/i18n.h"
#include "ft891bridge.h"

#ifdef Q_OS_ANDROID
#include <QPermissions>
#endif

#ifndef RR_VERSION
#define RR_VERSION "0.0.0"
#endif

namespace {

// Keys that act on the whole application, whatever has the focus.
//
// Space is push-to-talk on the desktop, except while a text field is being
// typed in. It is caught here, before any control sees it: a button that had
// kept the focus after a click would otherwise be pressed by it. The
// volume-down key is PTT on a phone, when the operator asked for it.
class GlobalKeys : public QObject {
public:
    explicit GlobalKeys(rr::Ft891Bridge *bridge, QObject *parent = nullptr)
        : QObject(parent), m_bridge(bridge) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
            return QObject::eventFilter(watched, event);
        auto *key = static_cast<QKeyEvent *>(event);
        const bool press = event->type() == QEvent::KeyPress;

        if (key->key() == Qt::Key_VolumeDown && m_bridge->pttOnVolumeKey()) {
            if (!key->isAutoRepeat()) m_bridge->setPtt(press);
            return true;
        }
        if (key->key() == Qt::Key_Space && !typing()) {
            if (!key->isAutoRepeat()) m_bridge->setPtt(press);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static bool typing()
    {
        const QObject *f = QGuiApplication::focusObject();
        return f && (f->inherits("QQuickTextInput") || f->inherits("QQuickTextEdit"));
    }

    rr::Ft891Bridge *m_bridge = nullptr;
};

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("FT891RemoteClient"));
    app.setOrganizationName(QStringLiteral("FT891Remote"));
    app.setApplicationVersion(QStringLiteral(RR_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/ft891remote.png")));

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("FT891RemoteClient")));

#ifdef Q_OS_ANDROID
    // Without the permission Oboe opens an input stream that only returns
    // silence, and reports no error.
    QMicrophonePermission micPermission;
    if (app.checkPermission(micPermission) == Qt::PermissionStatus::Undetermined)
        app.requestPermission(micPermission, [](const QPermission &) {});
#endif

    rr::Ft891Bridge bridge;
    app.installEventFilter(new GlobalKeys(&bridge, &app));

    // A singleton rather than a context property: Qt 6 discourages the
    // latter, which defeats QML compilation and type checking.
    qmlRegisterSingletonInstance("FT891Remote", 1, 0, "Radio", &bridge);

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    // Development aid: FT891_SCREENSHOT=file.png grabs the window after
    // FT891_SCREENSHOT_DELAY ms (default 4000) and quits. FT891_SIZE=400x800
    // resizes the window first, to look at the phone layout on a desktop;
    // FT891_TAB=n opens page n.
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        const QString size = qEnvironmentVariable("FT891_SIZE");
        if (size.contains(QLatin1Char('x')))
            win->resize(size.section(QLatin1Char('x'), 0, 0).toInt(), size.section(QLatin1Char('x'), 1, 1).toInt());
        if (qEnvironmentVariableIsSet("FT891_TAB"))
            win->setProperty("startTab", qEnvironmentVariableIntValue("FT891_TAB"));
        const QString shot = qEnvironmentVariable("FT891_SCREENSHOT");
        if (!shot.isEmpty()) {
            const int delay = qEnvironmentVariableIntValue("FT891_SCREENSHOT_DELAY");
            QTimer::singleShot(delay > 0 ? delay : 4000, &app, [win, shot] {
                win->grabWindow().save(shot);
                QCoreApplication::quit();
            });
        }
    }
    return app.exec();
}
