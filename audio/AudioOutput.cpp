#include "AudioOutput.h"

#include <algorithm>
#include <QDebug>
#include <QByteArray>
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

static bool looksLikeUsbOutputName(const QString& name)
{
    const QString n = name.toLower();
    return n.contains(QStringLiteral("usb")) ||
           n.contains(QStringLiteral("dac")) ||
           n.contains(QStringLiteral("audioquest")) ||
           n.contains(QStringLiteral("fiio")) ||
           n.contains(QStringLiteral("hiby"));
}

static bool looksLikeWiredOutputName(const QString& name)
{
    const QString n = name.toLower();
    return n.contains(QStringLiteral("headphone")) ||
           n.contains(QStringLiteral("headset")) ||
           n.contains(QStringLiteral("wired")) ||
           n.contains(QStringLiteral("jack")) ||
           n.contains(QStringLiteral("line out"));
}

static bool looksLikeBuiltinSpeakerName(const QString& name)
{
    const QString n = name.toLower();
    return n.contains(QStringLiteral("speaker")) ||
           n.contains(QStringLiteral("phone")) ||
           n.contains(QStringLiteral("built-in"));
}

static bool looksLikeEarpieceOutputName(const QString& name)
{
    const QString n = name.toLower();
    return n.contains(QStringLiteral("earpiece")) ||
           n.contains(QStringLiteral("receiver")) ||
           n.contains(QStringLiteral("handset"));
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
    m_format.setSampleRate(m_sampleRate);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged,
            this, &AudioOutput::onAudioOutputsChanged);
    refreshOutputDevices();

    m_keepAliveTimer.setInterval(m_keepAliveMs);
    connect(&m_keepAliveTimer, &QTimer::timeout,
            this, &AudioOutput::onKeepAliveTick);

    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout,
            this, &AudioOutput::onRestartTimeout);

#ifdef Q_OS_ANDROID
    m_devicePollTimer.setInterval(1200);
    connect(&m_devicePollTimer, &QTimer::timeout,
            this, &AudioOutput::onDevicePollTick);
    m_devicePollTimer.start();
#endif
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

QStringList AudioOutput::outputDeviceNames() const
{
    return m_outputDeviceNames;
}

QStringList AudioOutput::outputDeviceIds() const
{
    return m_outputDeviceIds;
}

QString AudioOutput::selectedOutputDeviceId() const
{
    return m_selectedOutputDeviceId;
}

int AudioOutput::sampleRate() const
{
    return m_sampleRate;
}

void AudioOutput::setSelectedOutputDeviceId(const QString& deviceId)
{
    const QString normalized = deviceId.trimmed();
    if (m_selectedOutputDeviceId == normalized)
        return;

    m_selectedOutputDeviceId = normalized;
    emit selectedOutputDeviceIdChanged();
    if (m_sink || m_device)
    {
        resetOutputSink();
        ensureStarted();
    }
}

void AudioOutput::setSampleRate(int sampleRate)
{
    const int normalized = qMax(8000, sampleRate);
    if (m_sampleRate == normalized)
        return;

    m_sampleRate = normalized;
    m_format.setSampleRate(m_sampleRate);
    emit sampleRateChanged();
    if (m_sink || m_device)
    {
        resetOutputSink();
        ensureStarted();
    }
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
    refreshOutputDevices();
    const QAudioDevice device = resolveOutputDevice();
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
#elif defined(Q_OS_WINDOWS)
    // Keep a slightly larger cushion on Windows, but avoid adding too much
    // playout latency.
    const int targetBufferMs = 100;
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
    connect(m_sink, &QAudioSink::stateChanged,
            this, &AudioOutput::onSinkStateChanged);
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
        m_resettingSink = true;
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
        m_resettingSink = false;
    }
    m_device = nullptr;
    m_activeOutputDeviceId.clear();
    m_pendingOutput.clear();
    m_resampler.reset();
}

void AudioOutput::scheduleRestart(int delayMs)
{
    if (m_restartScheduled && m_restartTimer.isActive())
        return;

    m_restartScheduled = true;
    m_restartTimer.start(qMax(50, delayMs));
}

void AudioOutput::onAudioOutputsChanged()
{
    const QByteArray previousActiveId = m_activeOutputDeviceId;
    refreshOutputDevices();

    // Re-open sink on route changes (e.g. device plug/unplug).
    if (!m_sink && !m_device)
        return;

    const QAudioDevice target = resolveOutputDevice();
    if (target.isNull())
        return;
    if (!previousActiveId.isEmpty() && previousActiveId == target.id())
        return;

    resetOutputSink();
    ensureStarted();
}

void AudioOutput::onDevicePollTick()
{
#ifdef Q_OS_ANDROID
    refreshOutputDevices();

    const QAudioDevice target = resolveOutputDevice();
    if (target.isNull())
        return;

    if (!m_activeOutputDeviceId.isEmpty() &&
        m_activeOutputDeviceId == target.id())
    {
        return;
    }

    if (m_sink || m_device)
    {
        resetOutputSink();
        ensureStarted();
    }
#endif
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

void AudioOutput::onRestartTimeout()
{
    m_restartScheduled = false;
    resetOutputSink();
    ensureStarted();
    flushPending();
}

void AudioOutput::onSinkStateChanged(QAudio::State state)
{
    if (!m_sink || m_resettingSink)
        return;

    if (state == QAudio::IdleState)
    {
        if (!m_pendingOutput.isEmpty())
            flushPending();
        return;
    }

    if (state != QAudio::StoppedState)
        return;

    const QtAudio::Error err = m_sink->error();
    if (err == QtAudio::NoError)
        return;

    qWarning("Audio output stopped with error=%d, scheduling restart=1",
             static_cast<int>(err));
    scheduleRestart(80);
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
        {
            if (m_sink->state() == QAudio::StoppedState &&
                m_sink->error() != QtAudio::NoError)
            {
                qWarning("Audio output write failed in stopped state error=%d",
                         static_cast<int>(m_sink->error()));
                scheduleRestart(80);
            }
            break;
        }

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

void AudioOutput::refreshOutputDevices()
{
    const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();

    QStringList names;
    QStringList ids;
    if (!defaultDevice.isNull())
    {
#ifdef Q_OS_ANDROID
        if (looksLikeEarpieceOutputName(defaultDevice.description()))
            names << QStringLiteral("System default");
        else
            names << QStringLiteral("System default (%1)").arg(defaultDevice.description());
#else
        names << QStringLiteral("System default (%1)").arg(defaultDevice.description());
#endif
    }
    else
        names << QStringLiteral("System default");
    ids << QString();

    for (const QAudioDevice& device : devices)
    {
#ifdef Q_OS_ANDROID
        if (looksLikeEarpieceOutputName(device.description()))
            continue;
#endif
        names << device.description();
        ids << encodeDeviceId(device.id());
    }

    if (m_outputDeviceNames != names || m_outputDeviceIds != ids)
    {
        m_outputDeviceNames = names;
        m_outputDeviceIds = ids;
        emit outputDevicesChanged();
    }

    if (!m_selectedOutputDeviceId.isEmpty() &&
        !m_outputDeviceIds.contains(m_selectedOutputDeviceId))
    {
        m_selectedOutputDeviceId.clear();
        emit selectedOutputDeviceIdChanged();
    }
}

QAudioDevice AudioOutput::resolveOutputDevice() const
{
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();

    if (!m_selectedOutputDeviceId.isEmpty())
    {
        const QByteArray wantedId = decodeDeviceId(m_selectedOutputDeviceId);
        if (!wantedId.isEmpty())
        {
            for (const QAudioDevice& device : outputs)
            {
                if (device.id() == wantedId)
                {
#ifdef Q_OS_ANDROID
                    if (looksLikeEarpieceOutputName(device.description()))
                        break;
#endif
                    return device;
                }
            }
        }
    }

#ifdef Q_OS_ANDROID
    // Prefer external devices (BT/USB/wired) over built-in speaker when available.
    QAudioDevice bestExternal;
    QAudioDevice bestSpeaker;
    int bestScore = -1;
    for (const QAudioDevice& device : outputs)
    {
        const QString name = device.description();
        if (looksLikeEarpieceOutputName(name))
            continue;
        int score = 0;
        if (looksLikeBluetoothOutputName(name))
            score = 300;
        else if (looksLikeUsbOutputName(name))
            score = 250;
        else if (looksLikeWiredOutputName(name))
            score = 200;
        else if (looksLikeBuiltinSpeakerName(name))
            score = 10;
        else
            score = 80;

        if (score > bestScore)
        {
            bestScore = score;
            bestExternal = device;
        }
        if (bestSpeaker.isNull() && looksLikeBuiltinSpeakerName(name))
            bestSpeaker = device;
    }

    if (!bestExternal.isNull() && bestScore >= 200)
        return bestExternal;

    if (!defaultDevice.isNull() && !looksLikeEarpieceOutputName(defaultDevice.description()))
        return defaultDevice;
    if (!bestSpeaker.isNull())
        return bestSpeaker;
#endif

    return defaultDevice;
}

QString AudioOutput::encodeDeviceId(const QByteArray& rawId)
{
    if (rawId.isEmpty())
        return QString();
    return QString::fromLatin1(rawId.toBase64());
}

QByteArray AudioOutput::decodeDeviceId(const QString& encodedId)
{
    if (encodedId.isEmpty())
        return QByteArray();
    return QByteArray::fromBase64(encodedId.toLatin1());
}
