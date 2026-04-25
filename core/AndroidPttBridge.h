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
    void setPreferredOutputRoute(int route);
    void setVolumePttEnabled(bool enabled);
    Q_INVOKABLE void playCueTone(int cueId);
    Q_INVOKABLE void playCueSound(const QString& uri, int cueId, int volumePercent);
    Q_INVOKABLE void prepareCueSound(const QString& uri, int cueId);

signals:
    void headsetButtonChanged(bool pressed);
    void volumeButtonPttChanged(bool pressed);
    void networkAvailabilityChanged(bool available);
    void audioRouteChanged();

private:
    explicit AndroidPttBridge(QObject* parent = nullptr);

#ifdef Q_OS_ANDROID
    static void nativeOnHeadsetPttChanged(JNIEnv* env, jclass clazz, jboolean pressed);
    static void nativeOnVolumePttChanged(JNIEnv* env, jclass clazz, jboolean pressed);
    static void nativeOnNetworkAvailabilityChanged(JNIEnv* env, jclass clazz, jboolean available);
    static void nativeOnAudioRouteChanged(JNIEnv* env, jclass clazz);
#endif

    bool m_initialized = false;
};
