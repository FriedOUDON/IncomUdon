#pragma once

#include <QObject>

#ifdef Q_OS_ANDROID
#include <jni.h>
#endif

class AndroidPttBridge : public QObject
{
    Q_OBJECT

public:
    static AndroidPttBridge& instance();

    bool initialize();
    void setPttAudioRouteEnabled(bool enabled);
    void setPreferCommunicationMode(bool enabled);
    Q_INVOKABLE void playCueTone(int cueId);

signals:
    void headsetButtonChanged(bool pressed);
    void networkAvailabilityChanged(bool available);

private:
    explicit AndroidPttBridge(QObject* parent = nullptr);

#ifdef Q_OS_ANDROID
    static void nativeOnHeadsetPttChanged(JNIEnv* env, jclass clazz, jboolean pressed);
    static void nativeOnNetworkAvailabilityChanged(JNIEnv* env, jclass clazz, jboolean available);
#endif

    bool m_initialized = false;
};
