#include "AudioOutput.h"

#include <algorithm>
#include <QDebug>
#include <QtEndian>
#include <cstring>

namespace {
static bool looksLikeBluetoothOutputName(const QString& name)
{
    const QString n = name.toLower();
    return n.contains(QStringLiteral("bluetooth")) ||
           n.contains(QStringLiteral("ble")) ||
           n.contains(QStringLiteral("headset")) ||
           n.contains(QStringLiteral("hands-free")) ||
           n.contains(QStringLiteral("wireless"));
}

static QAudioDevice resolvePreferredOutputDevice()
{
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
#ifdef Q_OS_ANDROID
    if (!defaultDevice.isNull() &&
        looksLikeBluetoothOutputName(defaultDevice.description()))
    {
        return defaultDevice;
    }

    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice& device : outputs)
    {
        if (looksLikeBluetoothOutputName(device.description()))
            return device;
    }
#endif
    return defaultDevice;
}

static QVector<qint16> toMonoInt16(const QByteArray& data)
{
    const int samples = data.size() / static_cast<int>(sizeof(qint16));
    QVector<qint16> out;
    out.reserve(samples);
    const char* ptr = data.constData();
    for (int i = 0; i < samples; ++i)
    {
        const qint16 v = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(ptr + i * static_cast<int>(sizeof(qint16))));
        out.append(v);
    }
    return out;
}

static void applyGain(QVector<qint16>& samples, float gain)
{
    if (samples.isEmpty())
        return;

    if (gain <= 0.0f)
    {
        std::fill(samples.begin(), samples.end(), 0);
        return;
    }

    if (qFuzzyCompare(gain, 1.0f))
        return;

    for (qint16& s : samples)
    {
        const float scaled = static_cast<float>(s) * gain;
        const int clamped = qBound(-32768, qRound(scaled), 32767);
        s = static_cast<qint16>(clamped);
    }
}

static QByteArray fromMonoInt16(const QVector<qint16>& samples, const QAudioFormat& format)
{
    const int channels = qMax(1, format.channelCount());
    const int bytesPerSample = format.bytesPerSample();
    if (bytesPerSample <= 0)
        return {};

    const QAudioFormat::SampleFormat fmt = format.sampleFormat();
    if (fmt != QAudioFormat::Int16 &&
        fmt != QAudioFormat::Int32 &&
        fmt != QAudioFormat::Float &&
        fmt != QAudioFormat::UInt8)
    {
        return {};
    }

    QByteArray out;
    out.resize(samples.size() * channels * bytesPerSample);
    char* dst = out.data();

    for (int i = 0; i < samples.size(); ++i)
    {
        const qint16 sample = samples[i];
        for (int ch = 0; ch < channels; ++ch)
        {
            char* writePtr = dst + (i * channels + ch) * bytesPerSample;
            switch (fmt)
            {
            case QAudioFormat::Int16:
            {
                const qint16 v = qToLittleEndian<qint16>(sample);
                std::memcpy(writePtr, &v, sizeof(v));
                break;
            }
            case QAudioFormat::Int32:
            {
                const qint32 v = qToLittleEndian<qint32>(
                    static_cast<qint32>(sample) << 16);
                std::memcpy(writePtr, &v, sizeof(v));
                break;
            }
            case QAudioFormat::Float:
            {
                const float v = static_cast<float>(sample) / 32768.0f;
                std::memcpy(writePtr, &v, sizeof(v));
                break;
            }
            case QAudioFormat::UInt8:
            {
                const quint8 v = static_cast<quint8>((sample >> 8) + 128);
                std::memcpy(writePtr, &v, sizeof(v));
                break;
            }
            default:
                break;
            }
        }
    }
    return out;
}
}

AudioOutput::AudioOutput(QObject* parent)
    : QObject(parent)
{
    m_format.setSampleRate(8000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged,
            this, &AudioOutput::onAudioOutputsChanged);

    m_keepAliveTimer.setInterval(m_keepAliveMs);
    connect(&m_keepAliveTimer, &QTimer::timeout,
            this, &AudioOutput::onKeepAliveTick);
}

int AudioOutput::lastFrameBytes() const
{
    return m_lastFrameBytes;
}

int AudioOutput::queuedMs() const
{
    if (!m_sink)
        return 0;

    int queuedBytes = m_pendingOutput.size();
    const int sinkBuffered = qMax(0, m_sink->bufferSize() - m_sink->bytesFree());
    queuedBytes += sinkBuffered;

    const int bytesPerSample = m_deviceFormat.bytesPerSample();
    const int channels = qMax(1, m_deviceFormat.channelCount());
    const int bytesPerSec = m_deviceFormat.sampleRate() * channels * bytesPerSample;
    if (bytesPerSec <= 0)
        return 0;

    return (queuedBytes * 1000) / bytesPerSec;
}

int AudioOutput::outputGainPercent() const
{
    return m_outputGainPercent;
}

void AudioOutput::setOutputGainPercent(int percent)
{
    m_outputGainPercent = qBound(0, percent, 400);
}

void AudioOutput::playFrame(const QByteArray& pcmFrame)
{
    ensureStarted();
    if (!m_device)
        return;

    m_lastFrameBytes = pcmFrame.size();
    if (m_lastFrameBytes > 0)
    {
        QVector<qint16> input = toMonoInt16(pcmFrame);
        const float gain = static_cast<float>(m_outputGainPercent) / 100.0f;
        applyGain(input, gain);
        QVector<qint16> resampled;
        m_resampler.push(input, resampled);
        const QByteArray deviceFrame = fromMonoInt16(resampled, m_deviceFormat);
        if (!deviceFrame.isEmpty())
        {
            m_pendingOutput.append(deviceFrame);
            trimPending();
        }
    }
    flushPending();
    emit lastFrameBytesChanged();
    emit framePlayed(m_lastFrameBytes);
}

void AudioOutput::ensureStarted()
{
    const QAudioDevice device = resolvePreferredOutputDevice();
    if (device.isNull())
        return;

    if (m_device && m_sink && !m_activeOutputDeviceId.isEmpty() &&
        m_activeOutputDeviceId == device.id())
    {
        return;
    }

    if (m_sink || m_device)
        resetOutputSink();

    m_deviceFormat = m_format;
    if (!device.isFormatSupported(m_deviceFormat))
    {
        QAudioFormat candidate = device.preferredFormat();
        candidate.setChannelCount(1);
        candidate.setSampleFormat(QAudioFormat::Int16);
        if (device.isFormatSupported(candidate))
        {
            m_deviceFormat = candidate;
        }
        else
        {
            qWarning("Audio output format not supported; using preferred format.");
            m_deviceFormat = device.preferredFormat();
        }
    }

    m_resampler.setRates(m_format.sampleRate(), m_deviceFormat.sampleRate());
    m_resampler.reset();
    m_pendingOutput.clear();

#ifdef Q_OS_ANDROID
    const int targetBufferMs = 160;
#else
    const int targetBufferMs = 80;
#endif

    const int bytesPerSample = m_deviceFormat.bytesPerSample();
    const int channels = qMax(1, m_deviceFormat.channelCount());
    int targetBufferBytes = 0;
    if (bytesPerSample > 0)
    {
        targetBufferBytes = (m_deviceFormat.sampleRate() * channels *
                             bytesPerSample * targetBufferMs) / 1000;
    }

    m_sink = new QAudioSink(device, m_deviceFormat, this);
    m_sink->setBufferSize(qMax(4096, targetBufferBytes));
    m_device = m_sink->start();
    m_activeOutputDeviceId = device.id();
    if (!m_device)
        qWarning("Failed to start audio output.");

    if (bytesPerSample > 0)
    {
        m_keepAliveBytes = (m_deviceFormat.sampleRate() * m_deviceFormat.channelCount() *
                            bytesPerSample * m_keepAliveMs) / 1000;
        m_outputFrameBytes = qMax(1, bytesPerSample * qMax(1, m_deviceFormat.channelCount()));
        const int bytesPerSec = m_deviceFormat.sampleRate() * m_outputFrameBytes;
        m_pendingSoftLimitBytes = qMax(targetBufferBytes, (bytesPerSec * 40) / 100);      // 400ms
        m_pendingHardLimitBytes = qMax(targetBufferBytes * 2, (bytesPerSec * 90) / 100);  // 900ms
        m_trimStepBytes = qMax(m_outputFrameBytes, bytesPerSec / 250); // ~4ms
    }
    if (m_keepAliveBytes <= 0)
        m_keepAliveBytes = 320;
    if (m_outputFrameBytes <= 0)
        m_outputFrameBytes = 2;
    if (m_pendingSoftLimitBytes <= 0)
        m_pendingSoftLimitBytes = 4096;
    if (m_pendingHardLimitBytes <= m_pendingSoftLimitBytes)
        m_pendingHardLimitBytes = m_pendingSoftLimitBytes + m_outputFrameBytes;
    if (m_trimStepBytes <= 0)
        m_trimStepBytes = m_outputFrameBytes;
    m_silenceFrame = QByteArray(m_keepAliveBytes, 0);

    m_lastWrite.restart();
    if (!m_keepAliveTimer.isActive())
        m_keepAliveTimer.start();
}

void AudioOutput::resetOutputSink()
{
    if (m_sink)
    {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_device = nullptr;
    m_activeOutputDeviceId.clear();
    m_pendingOutput.clear();
    m_resampler.reset();
}

void AudioOutput::onAudioOutputsChanged()
{
    // Re-open sink on route changes (e.g. Bluetooth A2DP/SCO switch on PTT).
    if (!m_sink && !m_device)
        return;
    resetOutputSink();
}

void AudioOutput::onKeepAliveTick()
{
    if (!m_device || !m_sink || m_keepAliveBytes <= 0)
        return;

    flushPending();

    if (!m_pendingOutput.isEmpty())
        return;

    if (m_lastWrite.isValid() && m_lastWrite.elapsed() < m_keepAliveMs)
        return;

    if (m_sink->bytesFree() < m_keepAliveBytes)
        return;

    m_pendingOutput.append(m_silenceFrame);
    flushPending();
}

void AudioOutput::flushPending()
{
    if (!m_device || !m_sink || m_pendingOutput.isEmpty())
        return;

    while (!m_pendingOutput.isEmpty())
    {
        const qint64 writable = m_sink->bytesFree();
        if (writable <= 0)
            break;

        const qint64 chunk = qMin<qint64>(writable, m_pendingOutput.size());
        if (chunk <= 0)
            break;

        const qint64 written = m_device->write(m_pendingOutput.constData(), chunk);
        if (written <= 0)
            break;

        m_pendingOutput.remove(0, static_cast<int>(written));
        m_lastWrite.restart();
    }
}

void AudioOutput::trimPending()
{
    if (m_pendingHardLimitBytes <= 0 || m_pendingOutput.size() <= m_pendingHardLimitBytes)
        return;

    int dropBytes = m_pendingOutput.size() - m_pendingSoftLimitBytes;
    if (dropBytes <= 0)
        return;

    const int align = qMax(1, m_outputFrameBytes);
    dropBytes -= (dropBytes % align);
    if (dropBytes <= 0)
        return;

    if (dropBytes > m_pendingOutput.size())
        dropBytes = m_pendingOutput.size();
    m_pendingOutput.remove(0, dropBytes);
}
