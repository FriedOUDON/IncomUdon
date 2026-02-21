#pragma once

#include <QObject>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QIODevice>
#include <QTimer>
#include <QtGlobal>

#include "audio/AudioResampler.h"

class AudioOutput : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int lastFrameBytes
               READ lastFrameBytes
               NOTIFY lastFrameBytesChanged)

public:
    explicit AudioOutput(QObject* parent = nullptr);

    int lastFrameBytes() const;
    int queuedMs() const;
    int outputGainPercent() const;
    void setOutputGainPercent(int percent);

public slots:
    void playFrame(const QByteArray& pcmFrame);

signals:
    void framePlayed(int bytes);
    void lastFrameBytesChanged();

private:
    void resetOutputSink();
    void ensureStarted();
    void onKeepAliveTick();
    void onAudioOutputsChanged();
    void flushPending();
    void trimPending();

    QAudioSink* m_sink = nullptr;
    QIODevice* m_device = nullptr;
    QAudioFormat m_format;
    QAudioFormat m_deviceFormat;
    AudioResampler m_resampler;
    QTimer m_keepAliveTimer;
    QElapsedTimer m_lastWrite;
    int m_keepAliveMs = 60;
    int m_keepAliveBytes = 0;
    QByteArray m_silenceFrame;
    QByteArray m_pendingOutput;
    QByteArray m_activeOutputDeviceId;
    QMediaDevices* m_mediaDevices = nullptr;
    int m_outputFrameBytes = 2;
    int m_pendingSoftLimitBytes = 0;
    int m_pendingHardLimitBytes = 0;
    int m_trimStepBytes = 0;
    int m_lastFrameBytes = 0;
    int m_outputGainPercent = 100;
};
