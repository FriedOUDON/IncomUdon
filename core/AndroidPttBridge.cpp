#include "AndroidPttBridge.h"

#include <QMetaObject>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

AndroidPttBridge& AndroidPttBridge::instance()
{
    static AndroidPttBridge bridge;
    return bridge;
}

AndroidPttBridge::AndroidPttBridge(QObject* parent)
    : QObject(parent)
{
}

bool AndroidPttBridge::initialize()
{
#ifdef Q_OS_ANDROID
    if (m_initialized)
        return true;

    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"onHeadsetPttChanged", "(Z)V",
         reinterpret_cast<void*>(&AndroidPttBridge::nativeOnHeadsetPttChanged)}
    };

    const bool ok = env.registerNativeMethods("com/friedoudon/incomudon/IncomUdonActivity",
                                              methods,
                                              sizeof(methods) / sizeof(methods[0]));
    if (!ok)
    {
        qWarning() << "Failed to register Android headset PTT native callback";
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return false;
    }

    m_initialized = true;
    return true;
#else
    return false;
#endif
}

void AndroidPttBridge::setPttAudioRouteEnabled(bool enabled)
{
#ifdef Q_OS_ANDROID
    if (!m_initialized)
        initialize();

    QJniObject::callStaticMethod<void>(
        "com/friedoudon/incomudon/IncomUdonActivity",
        "setPttAudioRoute",
        "(Z)V",
        static_cast<jboolean>(enabled ? JNI_TRUE : JNI_FALSE));
#else
    Q_UNUSED(enabled)
#endif
}

void AndroidPttBridge::setPreferCommunicationMode(bool enabled)
{
#ifdef Q_OS_ANDROID
    if (!m_initialized)
        initialize();

    QJniObject::callStaticMethod<void>(
        "com/friedoudon/incomudon/IncomUdonActivity",
        "setPreferCommunicationMode",
        "(Z)V",
        static_cast<jboolean>(enabled ? JNI_TRUE : JNI_FALSE));
#else
    Q_UNUSED(enabled)
#endif
}

void AndroidPttBridge::playCueTone(int cueId)
{
#ifdef Q_OS_ANDROID
    if (!m_initialized)
        initialize();

    QJniObject::callStaticMethod<void>(
        "com/friedoudon/incomudon/IncomUdonActivity",
        "playCueTone",
        "(I)V",
        static_cast<jint>(cueId));
#else
    Q_UNUSED(cueId)
#endif
}

#ifdef Q_OS_ANDROID
void AndroidPttBridge::nativeOnHeadsetPttChanged(JNIEnv* env, jclass clazz, jboolean pressed)
{
    Q_UNUSED(env)
    Q_UNUSED(clazz)

    const bool isPressed = (pressed == JNI_TRUE);
    QMetaObject::invokeMethod(&AndroidPttBridge::instance(),
                              [isPressed]() {
        emit AndroidPttBridge::instance().headsetButtonChanged(isPressed);
    },
                              Qt::QueuedConnection);
}
#endif
