#pragma once

#include <QObject>
#include <QByteArray>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudio>
#include <QMediaDevices>
#include <QIODevice>
#include <QStringList>
#include <QTimer>

#include "audio/AudioResampler.h"

class AudioInput : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int frameBytes
               READ frameBytes
               WRITE setFrameBytes
               NOTIFY frameBytesChanged)
    Q_PROPERTY(int intervalMs
               READ intervalMs
               WRITE setIntervalMs
               NOTIFY intervalMsChanged)
    Q_PROPERTY(bool running
               READ isRunning
               NOTIFY runningChanged)
    Q_PROPERTY(QStringList inputDeviceNames
               READ inputDeviceNames
               NOTIFY inputDevicesChanged)
    Q_PROPERTY(QStringList inputDeviceIds
               READ inputDeviceIds
               NOTIFY inputDevicesChanged)
    Q_PROPERTY(QString selectedInputDeviceId
               READ selectedInputDeviceId
               WRITE setSelectedInputDeviceId
               NOTIFY selectedInputDeviceIdChanged)
    Q_PROPERTY(bool noiseSuppressionEnabled
               READ noiseSuppressionEnabled
               WRITE setNoiseSuppressionEnabled
               NOTIFY noiseSuppressionEnabledChanged)
    Q_PROPERTY(int noiseSuppressionLevel
               READ noiseSuppressionLevel
               WRITE setNoiseSuppressionLevel
               NOTIFY noiseSuppressionLevelChanged)

public:
    explicit AudioInput(QObject* parent = nullptr);

    int frameBytes() const;
    void setFrameBytes(int bytes);

    int intervalMs() const;
    void setIntervalMs(int ms);

    bool isRunning() const;
    int inputGainPercent() const;
    void setInputGainPercent(int percent);
    QStringList inputDeviceNames() const;
    QStringList inputDeviceIds() const;
    QString selectedInputDeviceId() const;
    void setSelectedInputDeviceId(const QString& deviceId);
    bool noiseSuppressionEnabled() const;
    void setNoiseSuppressionEnabled(bool enabled);
    int noiseSuppressionLevel() const;
    void setNoiseSuppressionLevel(int level);

public slots:
    void start();
    void stop();

signals:
    void frameReady(const QByteArray& pcmFrame);
    void frameBytesChanged();
    void intervalMsChanged();
    void runningChanged();
    void microphonePermissionDenied();
    void inputDevicesChanged();
    void selectedInputDeviceIdChanged();
    void noiseSuppressionEnabledChanged();
    void noiseSuppressionLevelChanged();

private slots:
    void onReadyRead();
    void onSourceStateChanged(QAudio::State state);
    void onRestartTimeout();
    void refreshInputDevices();

private:
    void updateFormat();
    void clearSource();
    void scheduleRestart(int delayMs = 120);
    QAudioDevice resolveInputDevice() const;
    static QString encodeDeviceId(const QByteArray& rawId);
    static QByteArray decodeDeviceId(const QString& encodedId);
    void applyNoiseSuppression(QVector<qint16>& samples);

    QAudioSource* m_source = nullptr;
    QIODevice* m_device = nullptr;
    QAudioFormat m_format;
    QAudioFormat m_deviceFormat;
    QByteArray m_deviceBuffer;
    QByteArray m_buffer;
    QStringList m_inputDeviceNames;
    QStringList m_inputDeviceIds;
    QString m_selectedInputDeviceId;
    AudioResampler m_resampler;
    QMediaDevices* m_mediaDevices = nullptr;
    int m_frameBytes = 320;
    int m_intervalMs = 20;
    int m_inputGainPercent = 200;
    bool m_noiseSuppressionEnabled = false;
    int m_noiseSuppressionLevel = 45;
    float m_noiseFloor = 0.0f;
    float m_noiseGateGain = 1.0f;
    bool m_running = false;
    bool m_wantRunning = false;
    bool m_restartScheduled = false;
#if defined(Q_OS_ANDROID) && QT_CONFIG(permissions)
    bool m_permissionRequestInFlight = false;
#endif
    QTimer m_restartTimer;
};
