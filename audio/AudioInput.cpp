#include "AudioInput.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDebug>
#include <QPermission>
#include <QPermissions>
#include <QtEndian>
#include <cstring>
#include <cmath>

namespace {
static QVector<qint16> toMonoInt16(const QByteArray& data, const QAudioFormat& format)
{
    const int channels = qMax(1, format.channelCount());
    const int bytesPerSample = format.bytesPerSample();
    if (bytesPerSample <= 0)
        return {};

    const int frameBytes = bytesPerSample * channels;
    const int sampleCount = data.size() / frameBytes;
    QVector<qint16> out;
    out.reserve(sampleCount);

    const char* ptr = data.constData();
    for (int i = 0; i < sampleCount; ++i)
    {
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const char* samplePtr = ptr + (i * channels + ch) * bytesPerSample;
            switch (format.sampleFormat())
            {
            case QAudioFormat::Int16:
                sum += qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(samplePtr));
                break;
            case QAudioFormat::Int32:
                sum += static_cast<double>(qFromLittleEndian<qint32>(
                    reinterpret_cast<const uchar*>(samplePtr))) / 65536.0;
                break;
            case QAudioFormat::Float:
            {
                float v = 0.0f;
                std::memcpy(&v, samplePtr, sizeof(float));
                sum += static_cast<double>(v) * 32767.0;
                break;
            }
            case QAudioFormat::UInt8:
                sum += (static_cast<int>(static_cast<unsigned char>(*samplePtr)) - 128) * 256;
                break;
            default:
                break;
            }
        }

        double v = sum / static_cast<double>(channels);
        if (v > 32767.0)
            v = 32767.0;
        else if (v < -32768.0)
            v = -32768.0;
        out.append(static_cast<qint16>(qRound(v)));
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
}

AudioInput::AudioInput(QObject* parent)
    : QObject(parent)
{
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged,
            this, &AudioInput::refreshInputDevices);
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout,
            this, &AudioInput::onRestartTimeout);
    updateFormat();
    refreshInputDevices();
}

int AudioInput::frameBytes() const
{
    return m_frameBytes;
}

void AudioInput::setFrameBytes(int bytes)
{
    if (m_frameBytes == bytes)
        return;

    m_frameBytes = bytes;
    emit frameBytesChanged();
}

int AudioInput::intervalMs() const
{
    return m_intervalMs;
}

void AudioInput::setIntervalMs(int ms)
{
    if (m_intervalMs == ms)
        return;

    m_intervalMs = ms;
    updateFormat();
    emit intervalMsChanged();
}

bool AudioInput::isRunning() const
{
    return m_running;
}

int AudioInput::inputGainPercent() const
{
    return m_inputGainPercent;
}

void AudioInput::setInputGainPercent(int percent)
{
    m_inputGainPercent = qBound(0, percent, 300);
}

QStringList AudioInput::inputDeviceNames() const
{
    return m_inputDeviceNames;
}

QStringList AudioInput::inputDeviceIds() const
{
    return m_inputDeviceIds;
}

QString AudioInput::selectedInputDeviceId() const
{
    return m_selectedInputDeviceId;
}

void AudioInput::setSelectedInputDeviceId(const QString& deviceId)
{
    const QString normalized = deviceId.trimmed();
    if (m_selectedInputDeviceId == normalized)
        return;

    m_selectedInputDeviceId = normalized;
    emit selectedInputDeviceIdChanged();

    if (!m_wantRunning)
        return;

    clearSource();
    if (m_running)
    {
        m_running = false;
        emit runningChanged();
    }
    scheduleRestart(30);
}

bool AudioInput::noiseSuppressionEnabled() const
{
    return m_noiseSuppressionEnabled;
}

void AudioInput::setNoiseSuppressionEnabled(bool enabled)
{
    if (m_noiseSuppressionEnabled == enabled)
        return;
    m_noiseSuppressionEnabled = enabled;
    m_noiseFloor = 0.0f;
    m_noiseGateGain = 1.0f;
    emit noiseSuppressionEnabledChanged();
}

int AudioInput::noiseSuppressionLevel() const
{
    return m_noiseSuppressionLevel;
}

void AudioInput::setNoiseSuppressionLevel(int level)
{
    const int normalized = qBound(0, level, 100);
    if (m_noiseSuppressionLevel == normalized)
        return;
    m_noiseSuppressionLevel = normalized;
    if (m_noiseSuppressionLevel <= 0)
        m_noiseGateGain = 1.0f;
    emit noiseSuppressionLevelChanged();
}

void AudioInput::start()
{
    m_wantRunning = true;
    m_restartTimer.stop();
    m_restartScheduled = false;

#if defined(Q_OS_ANDROID) && QT_CONFIG(permissions)
    QCoreApplication* app = QCoreApplication::instance();
    if (app)
    {
        const QMicrophonePermission permission;
        const Qt::PermissionStatus status = app->checkPermission(permission);
        if (status != Qt::PermissionStatus::Granted)
        {
            if (m_permissionRequestInFlight)
                return;

            m_permissionRequestInFlight = true;
            app->requestPermission(permission, this, [this](const QPermission& result) {
                m_permissionRequestInFlight = false;
                if (result.status() == Qt::PermissionStatus::Granted)
                {
                    if (m_wantRunning)
                        start();
                    return;
                }
                emit microphonePermissionDenied();
            });
            return;
        }
    }
#endif

    if (m_running && m_source && m_device)
        return;

    if (m_source || m_device)
        clearSource();

    const QAudioDevice device = resolveInputDevice();
    if (device.isNull())
    {
        qWarning("No audio input device available.");
        scheduleRestart(250);
        return;
    }

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
            qWarning("Audio input format not supported; using preferred format.");
            m_deviceFormat = device.preferredFormat();
        }
    }

    m_resampler.setRates(m_deviceFormat.sampleRate(), m_format.sampleRate());
    m_resampler.reset();
    m_deviceBuffer.clear();
    m_buffer.clear();
    m_noiseFloor = 0.0f;
    m_noiseGateGain = 1.0f;

    m_source = new QAudioSource(device, m_deviceFormat, this);
    m_source->setBufferSize(m_frameBytes * 4);
    connect(m_source, &QAudioSource::stateChanged,
            this, &AudioInput::onSourceStateChanged);
    m_device = m_source->start();
    if (!m_device)
    {
        qWarning("Failed to start audio input.");
        clearSource();
        scheduleRestart(160);
        return;
    }

    connect(m_device, &QIODevice::readyRead,
            this, &AudioInput::onReadyRead);

    if (!m_running)
    {
        m_running = true;
        emit runningChanged();
    }
}

void AudioInput::stop()
{
    m_wantRunning = false;
    m_restartTimer.stop();
    m_restartScheduled = false;

    if (!m_running && !m_source && !m_device)
        return;

    clearSource();
    if (m_running)
    {
        m_running = false;
        emit runningChanged();
    }
}

void AudioInput::onReadyRead()
{
    if (!m_device || m_frameBytes <= 0)
        return;

    m_deviceBuffer.append(m_device->readAll());

    const int bytesPerSample = m_deviceFormat.bytesPerSample();
    const int channels = qMax(1, m_deviceFormat.channelCount());
    const int frameBytes = bytesPerSample * channels;
    if (frameBytes <= 0)
        return;

    const int usableBytes = (m_deviceBuffer.size() / frameBytes) * frameBytes;
    if (usableBytes <= 0)
        return;

    const QByteArray raw = m_deviceBuffer.left(usableBytes);
    m_deviceBuffer.remove(0, usableBytes);

    const QVector<qint16> mono = toMonoInt16(raw, m_deviceFormat);
    QVector<qint16> resampled;
    m_resampler.push(mono, resampled);
    if (!resampled.isEmpty())
    {
        applyNoiseSuppression(resampled);
        const float gain = static_cast<float>(m_inputGainPercent) / 100.0f;
        applyGain(resampled, gain);
        m_buffer.append(reinterpret_cast<const char*>(resampled.constData()),
                        resampled.size() * static_cast<int>(sizeof(qint16)));
    }

    while (m_buffer.size() >= m_frameBytes)
    {
        const QByteArray frame = m_buffer.left(m_frameBytes);
        m_buffer.remove(0, m_frameBytes);
        emit frameReady(frame);
    }
}

void AudioInput::updateFormat()
{
    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_format = format;

    const int bytesPerSample = m_format.bytesPerSample();
    if (bytesPerSample > 0)
    {
        const int computed = (m_format.sampleRate() * m_format.channelCount() *
                              bytesPerSample * m_intervalMs) / 1000;
        if (computed > 0 && computed != m_frameBytes)
        {
            m_frameBytes = computed;
            emit frameBytesChanged();
        }
    }
}

void AudioInput::clearSource()
{
    if (m_device)
    {
        disconnect(m_device, nullptr, this, nullptr);
        m_device = nullptr;
    }
    if (m_source)
    {
        disconnect(m_source, nullptr, this, nullptr);
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_deviceBuffer.clear();
    m_buffer.clear();
    m_resampler.reset();
    m_noiseFloor = 0.0f;
    m_noiseGateGain = 1.0f;
}

void AudioInput::scheduleRestart(int delayMs)
{
    if (!m_wantRunning)
        return;
    if (m_restartScheduled && m_restartTimer.isActive())
        return;
    m_restartScheduled = true;
    m_restartTimer.start(qMax(50, delayMs));
}

void AudioInput::onRestartTimeout()
{
    m_restartScheduled = false;
    if (!m_wantRunning)
        return;
    start();
}

void AudioInput::onSourceStateChanged(QAudio::State state)
{
    if (!m_source)
        return;

    if (state != QAudio::StoppedState)
        return;

    const QtAudio::Error err = m_source->error();
    const bool shouldRecover = m_wantRunning;
    if (err != QtAudio::NoError)
    {
        qWarning("Audio input stopped with error=%d, scheduling restart=%d",
                 static_cast<int>(err),
                 shouldRecover ? 1 : 0);
    }

    clearSource();
    if (m_running)
    {
        m_running = false;
        emit runningChanged();
    }

    if (shouldRecover)
        scheduleRestart(120);
}

void AudioInput::refreshInputDevices()
{
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioInput();

    QStringList names;
    QStringList ids;
    if (!defaultDevice.isNull())
        names << QStringLiteral("System default (%1)").arg(defaultDevice.description());
    else
        names << QStringLiteral("System default");
    ids << QString();

    for (const QAudioDevice& device : devices)
    {
        names << device.description();
        ids << encodeDeviceId(device.id());
    }

    if (m_inputDeviceNames != names || m_inputDeviceIds != ids)
    {
        m_inputDeviceNames = names;
        m_inputDeviceIds = ids;
        emit inputDevicesChanged();
    }

    if (!m_selectedInputDeviceId.isEmpty() && !m_inputDeviceIds.contains(m_selectedInputDeviceId))
    {
        m_selectedInputDeviceId.clear();
        emit selectedInputDeviceIdChanged();
    }
}

QAudioDevice AudioInput::resolveInputDevice() const
{
    if (m_selectedInputDeviceId.isEmpty())
        return QMediaDevices::defaultAudioInput();

    const QByteArray wantedId = decodeDeviceId(m_selectedInputDeviceId);
    if (wantedId.isEmpty())
        return QMediaDevices::defaultAudioInput();

    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice& device : devices)
    {
        if (device.id() == wantedId)
            return device;
    }
    return QMediaDevices::defaultAudioInput();
}

QString AudioInput::encodeDeviceId(const QByteArray& rawId)
{
    if (rawId.isEmpty())
        return QString();
    return QString::fromLatin1(rawId.toBase64());
}

QByteArray AudioInput::decodeDeviceId(const QString& encodedId)
{
    if (encodedId.isEmpty())
        return QByteArray();
    return QByteArray::fromBase64(encodedId.toLatin1());
}

void AudioInput::applyNoiseSuppression(QVector<qint16>& samples)
{
    if (samples.isEmpty())
        return;

    if (!m_noiseSuppressionEnabled || m_noiseSuppressionLevel <= 0)
    {
        m_noiseGateGain = 1.0f;
        return;
    }

    double energy = 0.0;
    float peakAbs = 0.0f;
    for (const qint16 s : samples)
    {
        const float v = static_cast<float>(s);
        const float a = std::fabs(v);
        if (a > peakAbs)
            peakAbs = a;
        energy += static_cast<double>(v) * static_cast<double>(v);
    }
    const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(samples.size())));
    if (m_noiseFloor < 1.0f)
        m_noiseFloor = qMax(8.0f, rms * 0.8f);

    const bool likelyNoiseOnly = (rms < m_noiseFloor * 1.8f) && (peakAbs < 4000.0f);
    const float floorRise = likelyNoiseOnly ? 0.20f : 0.01f;
    const float floorFall = likelyNoiseOnly ? 0.02f : 0.005f;
    if (rms > m_noiseFloor)
        m_noiseFloor += (rms - m_noiseFloor) * floorRise;
    else
        m_noiseFloor += (rms - m_noiseFloor) * floorFall;
    m_noiseFloor = qBound(6.0f, m_noiseFloor, 9000.0f);

    const float snr = rms / qMax(1.0f, m_noiseFloor);
    const float strength = static_cast<float>(m_noiseSuppressionLevel) / 100.0f;
    const float gateStart = 1.2f + 0.8f * strength;
    const float gateFull = gateStart + 1.8f;
    const float minGain = 1.0f - 0.92f * strength;

    float targetGain = 1.0f;
    if (snr <= gateStart)
    {
        targetGain = minGain;
    }
    else if (snr < gateFull)
    {
        const float t = (snr - gateStart) / (gateFull - gateStart);
        targetGain = minGain + (1.0f - minGain) * t;
    }

    const float attack = 0.45f;
    const float release = 0.08f;
    if (targetGain < m_noiseGateGain)
        m_noiseGateGain += (targetGain - m_noiseGateGain) * attack;
    else
        m_noiseGateGain += (targetGain - m_noiseGateGain) * release;

    const float finalGain = qBound(0.03f, m_noiseGateGain, 1.0f);
    if (finalGain >= 0.999f)
        return;

    for (qint16& s : samples)
    {
        const float scaled = static_cast<float>(s) * finalGain;
        s = static_cast<qint16>(qBound(-32768, qRound(scaled), 32767));
    }
}
