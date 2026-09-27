#include "androidservice.h"

// QtGlobal first, unconditionally: it defines Q_OS_ANDROID. Without it the
// includes below are skipped while the code that needs them still compiles
// into confusing errors.
#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QString>
#include <jni.h>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace rr {

static VolumePttSink *g_pttSink = nullptr;
void setVolumePttSink(VolumePttSink *sink) { g_pttSink = sink; }

#ifdef Q_OS_ANDROID

// context() returned a jobject before Qt 6.7, a QJniObject since.
static QJniObject androidContext()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return QNativeInterface::QAndroidApplication::context();
#else
    return QJniObject(QNativeInterface::QAndroidApplication::context());
#endif
}

// setClassName avoids building a ComponentName: one JNI call fewer.
static QJniObject serviceIntent()
{
    QJniObject intent("android/content/Intent", "()V");
    QJniObject className = QJniObject::fromString(
        QStringLiteral("org.ft891remote.client.Ft891RemoteService"));
    const QJniObject context = androidContext();
    intent.callObjectMethod(
        "setClassName",
        "(Landroid/content/Context;Ljava/lang/String;)Landroid/content/Intent;",
        context.object(), className.object());
    return intent;
}

void startAndroidService()
{
    const QJniObject context = androidContext();
    const QJniObject intent = serviceIntent();
    // startForegroundService is required from Android 8 on.
    context.callObjectMethod("startForegroundService",
                             "(Landroid/content/Intent;)Landroid/content/ComponentName;",
                             intent.object());
}

int androidStatusBarHeight()
{
    QJniObject context = androidContext();
    QJniObject resources = context.callObjectMethod(
        "getResources", "()Landroid/content/res/Resources;");
    if (!resources.isValid()) return 0;

    QJniObject name = QJniObject::fromString(QStringLiteral("status_bar_height"));
    QJniObject defType = QJniObject::fromString(QStringLiteral("dimen"));
    QJniObject defPackage = QJniObject::fromString(QStringLiteral("android"));
    const jint id = resources.callMethod<jint>(
        "getIdentifier",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
        name.object(), defType.object(), defPackage.object());
    if (id <= 0) return 0;

    return int(resources.callMethod<jint>("getDimensionPixelSize", "(I)I", id));
}

void stopAndroidService()
{
    const QJniObject context = androidContext();
    const QJniObject intent = serviceIntent();
    context.callMethod<jboolean>("stopService", "(Landroid/content/Intent;)Z",
                                 intent.object());
}

#else

void startAndroidService() {}
void stopAndroidService() {}
int androidStatusBarHeight() { return 0; }

#endif

} // namespace rr

#ifdef Q_OS_ANDROID
// Qt lets the volume keys through to the system: they never reach C++. The
// Java activity intercepts them and calls these two functions.
extern "C" JNIEXPORT jboolean JNICALL
Java_org_ft891remote_client_Ft891RemoteActivity_volumePttEnabled(JNIEnv *, jclass)
{
    return rr::g_pttSink && rr::g_pttSink->volumePttWanted() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_ft891remote_client_Ft891RemoteActivity_volumePtt(JNIEnv *, jclass, jboolean pressed)
{
    if (rr::g_pttSink) rr::g_pttSink->volumePttChanged(pressed == JNI_TRUE);
}
#endif
