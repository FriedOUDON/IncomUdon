#include "Codec2Wrapper.h"
#include "net/packet.h"

#include <QtGlobal>
#include <QDir>
#include <QLibrary>
#include <QMutexLocker>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
QString normalizeDynamicLibraryPath(const QString& path, QString* error);
}

#if defined(Q_OS_ANDROID)
#include <android/log.h>
#include <QJniEnvironment>
#include <QJniObject>
#endif

#ifdef INCOMUDON_USE_OPUS
void Codec2Wrapper::unloadOpusLibrary()
{
    if (!m_opusLibrary)
        return;
    if (m_opusLibrary->isLoaded())
        m_opusLibrary->unload();
}

void Codec2Wrapper::clearOpusApi()
{
    m_opusEncoderCreate = nullptr;
    m_opusEncoderDestroy = nullptr;
    m_opusEncode = nullptr;
    m_opusEncoderCtl = nullptr;
    m_opusDecoderCreate = nullptr;
    m_opusDecoderDestroy = nullptr;
    m_opusDecode = nullptr;
}

QString Codec2Wrapper::normalizeOpusLibraryPath(const QString& path, QString* error) const
{
    return normalizeDynamicLibraryPath(path, error);
}

void Codec2Wrapper::setOpusLibraryLoadedInternal(bool loaded)
{
    if (m_opusLibraryLoaded == loaded)
        return;
    m_opusLibraryLoaded = loaded;
    emit opusLibraryLoadedChanged();
}

void Codec2Wrapper::setOpusLibraryErrorInternal(const QString& error)
{
    if (m_opusLibraryError == error)
        return;
    m_opusLibraryError = error;
    emit opusLibraryErrorChanged();
}

void Codec2Wrapper::refreshOpusLibrary()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (!m_opusLibrary)
        return;

    clearOpusApi();
    unloadOpusLibrary();

    const bool explicitPath = !m_opusLibraryPath.trimmed().isEmpty();
    QString normalizedError;
    const QString normalizedPath = normalizeOpusLibraryPath(m_opusLibraryPath, &normalizedError);
    if (explicitPath && normalizedPath.isEmpty())
    {
        setOpusLibraryLoadedInternal(false);
        if (normalizedError.isEmpty())
            normalizedError = QStringLiteral("Invalid opus library path");
        setOpusLibraryErrorInternal(normalizedError);
        return;
    }

    // Runtime loading is optional and used only when user specifies a path.
    if (!explicitPath)
    {
        setOpusLibraryLoadedInternal(false);
        setOpusLibraryErrorInternal(QString());
        return;
    }

    m_opusLibrary->setFileName(normalizedPath);
    if (!m_opusLibrary->load())
    {
        setOpusLibraryLoadedInternal(false);
        setOpusLibraryErrorInternal(QStringLiteral("Failed to load opus library: %1")
                                        .arg(m_opusLibrary->errorString()));
        return;
    }

    m_opusEncoderCreate = reinterpret_cast<OpusEncoderCreateFn>(
        m_opusLibrary->resolve("opus_encoder_create"));
    m_opusEncoderDestroy = reinterpret_cast<OpusEncoderDestroyFn>(
        m_opusLibrary->resolve("opus_encoder_destroy"));
    m_opusEncode = reinterpret_cast<OpusEncodeFn>(
        m_opusLibrary->resolve("opus_encode"));
    m_opusEncoderCtl = reinterpret_cast<OpusEncoderCtlFn>(
        m_opusLibrary->resolve("opus_encoder_ctl"));
    m_opusDecoderCreate = reinterpret_cast<OpusDecoderCreateFn>(
        m_opusLibrary->resolve("opus_decoder_create"));
    m_opusDecoderDestroy = reinterpret_cast<OpusDecoderDestroyFn>(
        m_opusLibrary->resolve("opus_decoder_destroy"));
    m_opusDecode = reinterpret_cast<OpusDecodeFn>(
        m_opusLibrary->resolve("opus_decode"));

    const bool symbolsOk = m_opusEncoderCreate &&
                           m_opusEncoderDestroy &&
                           m_opusEncode &&
                           m_opusEncoderCtl &&
                           m_opusDecoderCreate &&
                           m_opusDecoderDestroy &&
                           m_opusDecode;
    if (symbolsOk)
    {
        setOpusLibraryLoadedInternal(true);
        setOpusLibraryErrorInternal(QString());
        return;
    }

    clearOpusApi();
    unloadOpusLibrary();
    setOpusLibraryLoadedInternal(false);
    setOpusLibraryErrorInternal(
        QStringLiteral("Required opus symbols were not found in: %1").arg(normalizedPath));
}
#endif

#ifdef INCOMUDON_USE_OPUS
#include <opus/opus.h>
#endif

namespace
{
#ifdef INCOMUDON_USE_CODEC2
// Keep local mode constants so build does not depend on codec2 public headers.
constexpr int kCodec2Mode3200 = 0;
constexpr int kCodec2Mode2400 = 1;
constexpr int kCodec2Mode1600 = 2;
constexpr int kCodec2Mode700C = 8;
constexpr int kCodec2Mode450 = 10;
#endif

#ifdef INCOMUDON_USE_CODEC2
QRecursiveMutex& codec2ApiMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}
#endif

void logCodec2Status(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
#if defined(Q_OS_ANDROID)
    __android_log_vprint(ANDROID_LOG_WARN, "IncomUdon", fmt, args);
#else
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    qWarning("%s", buffer);
#endif
    va_end(args);
}

#if defined(Q_OS_ANDROID)
QString copyAndroidContentUriToLocalPath(const QString& uriText, QString* error)
{
    if (error)
        error->clear();

    QJniObject jUriText = QJniObject::fromString(uriText);
    QJniObject jResult = QJniObject::callStaticObjectMethod(
        "com/example/incomudon/IncomUdonActivity",
        "copyContentUriToLocalLib",
        "(Ljava/lang/String;)Ljava/lang/String;",
        jUriText.object<jstring>());

    QJniEnvironment env;
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (error)
            *error = QStringLiteral("Failed to access Android content URI");
        return QString();
    }

    if (!jResult.isValid())
    {
        if (error)
            *error = QStringLiteral("Failed to resolve content URI: %1").arg(uriText);
        return QString();
    }

    const QString localPath = jResult.toString();
    if (localPath.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Could not copy selected library to app storage");
        return QString();
    }
    return QDir::toNativeSeparators(localPath);
}
#endif

QString normalizeDynamicLibraryPath(const QString& path, QString* error)
{
    if (error)
        error->clear();

    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();

    const QUrl url(trimmed);
    if (url.isValid())
    {
        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("file"))
            return QDir::toNativeSeparators(url.toLocalFile());
#if defined(Q_OS_ANDROID)
        if (scheme == QStringLiteral("content"))
            return copyAndroidContentUriToLocalPath(trimmed, error);
#endif
    }
#if defined(Q_OS_ANDROID)
    if (trimmed.startsWith(QStringLiteral("content://"), Qt::CaseInsensitive))
        return copyAndroidContentUriToLocalPath(trimmed, error);
#endif
    return QDir::toNativeSeparators(trimmed);
}
}

Codec2Wrapper::Codec2Wrapper(QObject* parent)
    : QObject(parent)
{
#ifdef INCOMUDON_USE_CODEC2
    m_codec2Library = new QLibrary(this);
    refreshCodec2Library();
    logCodec2Status("INCOMUDON_USE_CODEC2=1 (runtime load)");
#else
    m_codec2LibraryError = QStringLiteral("Codec2 support disabled at build time");
    logCodec2Status("INCOMUDON_USE_CODEC2=0 (disabled at build time)");
#endif
#ifdef INCOMUDON_USE_OPUS
    m_opusLibrary = new QLibrary(this);
    refreshOpusLibrary();
#else
    m_opusLibraryError = QStringLiteral("Opus support disabled at build time");
#endif
    updateCodec();
}

Codec2Wrapper::~Codec2Wrapper()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
#ifdef INCOMUDON_USE_CODEC2
    if (m_codec && m_codec2Destroy)
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        m_codec2Destroy(m_codec);
        m_codec = nullptr;
    }
    unloadCodec2Library();
    delete m_codec2Library;
    m_codec2Library = nullptr;
#endif
#ifdef INCOMUDON_USE_OPUS
    if (m_opusEncoder)
    {
        if (m_opusUsingRuntimeApi && m_opusEncoderDestroy)
            m_opusEncoderDestroy(m_opusEncoder);
        else
            opus_encoder_destroy(m_opusEncoder);
        m_opusEncoder = nullptr;
    }
    if (m_opusDecoder)
    {
        if (m_opusUsingRuntimeApi && m_opusDecoderDestroy)
            m_opusDecoderDestroy(m_opusDecoder);
        else
            opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
    m_opusUsingRuntimeApi = false;
    unloadOpusLibrary();
    delete m_opusLibrary;
    m_opusLibrary = nullptr;
#endif
}

int Codec2Wrapper::codecType() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codecType;
}

void Codec2Wrapper::setCodecType(int type)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    int normalized = CodecTypeCodec2;
    if (type == CodecTypeOpus)
        normalized = CodecTypeOpus;

#ifndef INCOMUDON_USE_OPUS
    if (normalized == CodecTypeOpus)
        normalized = CodecTypeCodec2;
#endif

    if (m_codecType == normalized)
        return;

    m_codecType = normalized;
    updateCodec();
    emit codecTypeChanged();
}

int Codec2Wrapper::mode() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_mode;
}

void Codec2Wrapper::setMode(int mode)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    const int normalized = normalizeMode(mode);
    if (m_mode == normalized)
        return;

    m_mode = normalized;
    updateCodec();
    emit modeChanged();
}

int Codec2Wrapper::frameBytes() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_frameBytes;
}

void Codec2Wrapper::setFrameBytes(int bytes)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_frameBytes == bytes)
        return;

    m_frameBytes = bytes;
    emit frameBytesChanged();
}

int Codec2Wrapper::pcmFrameBytes() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_pcmFrameBytes;
}

int Codec2Wrapper::frameMs() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_frameMs;
}

bool Codec2Wrapper::forcePcm() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_forcePcm;
}

void Codec2Wrapper::setForcePcm(bool force)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm == force)
        return;

    m_forcePcm = force;
    updateCodec();
    emit forcePcmChanged();
}

bool Codec2Wrapper::codec2Active() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2Active;
}

bool Codec2Wrapper::opusActive() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusActive;
}

QString Codec2Wrapper::opusLibraryPath() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusLibraryPath;
}

void Codec2Wrapper::setOpusLibraryPath(const QString& path)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_opusLibraryPath == path)
        return;

    m_opusLibraryPath = path;
    emit opusLibraryPathChanged();

#ifdef INCOMUDON_USE_OPUS
    refreshOpusLibrary();
#else
    if (m_opusLibraryError != QStringLiteral("Opus support disabled at build time"))
    {
        m_opusLibraryError = QStringLiteral("Opus support disabled at build time");
        emit opusLibraryErrorChanged();
    }
#endif
    updateCodec();
}

bool Codec2Wrapper::opusLibraryLoaded() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusLibraryLoaded;
}

QString Codec2Wrapper::opusLibraryError() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_opusLibraryError;
}

int Codec2Wrapper::activeCodecTransportId() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm)
        return Proto::CODEC_TRANSPORT_PCM;
    if (m_opusActive)
        return Proto::CODEC_TRANSPORT_OPUS;
    if (m_codec2Active)
        return Proto::CODEC_TRANSPORT_CODEC2;
    return Proto::CODEC_TRANSPORT_PCM;
}

QString Codec2Wrapper::codec2LibraryPath() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2LibraryPath;
}

void Codec2Wrapper::setCodec2LibraryPath(const QString& path)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_codec2LibraryPath == path)
        return;

    m_codec2LibraryPath = path;
    emit codec2LibraryPathChanged();

#ifdef INCOMUDON_USE_CODEC2
    refreshCodec2Library();
#else
    if (m_codec2LibraryError != QStringLiteral("Codec2 support disabled at build time"))
    {
        m_codec2LibraryError = QStringLiteral("Codec2 support disabled at build time");
        emit codec2LibraryErrorChanged();
    }
#endif

    updateCodec();
}

bool Codec2Wrapper::codec2LibraryLoaded() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2LibraryLoaded;
}

QString Codec2Wrapper::codec2LibraryError() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_codec2LibraryError;
}

QByteArray Codec2Wrapper::encode(const QByteArray& pcmFrame) const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm || m_pcmFrameBytes <= 0)
        return pcmFrame;

#ifdef INCOMUDON_USE_OPUS
    if (m_opusActive && m_codecType == CodecTypeOpus && m_opusEncoder)
    {
        const int expectedSamples = m_pcmFrameBytes / static_cast<int>(sizeof(opus_int16));
        QVector<opus_int16> inputSamples(expectedSamples, 0);
        const int copyBytes = qMin(pcmFrame.size(), m_pcmFrameBytes);
        if (copyBytes > 0)
            std::memcpy(inputSamples.data(), pcmFrame.constData(), copyBytes);

        QByteArray output(512, 0);
        const int encodedBytes = (m_opusUsingRuntimeApi && m_opusEncode)
            ? m_opusEncode(m_opusEncoder,
                           inputSamples.constData(),
                           expectedSamples,
                           reinterpret_cast<unsigned char*>(output.data()),
                           output.size())
            : opus_encode(m_opusEncoder,
                          inputSamples.constData(),
                          expectedSamples,
                          reinterpret_cast<unsigned char*>(output.data()),
                          output.size());
        if (encodedBytes <= 0)
            return QByteArray();
        output.truncate(encodedBytes);
        return output;
    }
#endif

#ifdef INCOMUDON_USE_CODEC2
    if (!m_codec || !m_codec2Encode || m_frameBytes <= 0)
        return pcmFrame;

    const int samples = m_pcmFrameBytes / static_cast<int>(sizeof(short));
    QVector<short> inputSamples(samples);

    const int copyBytes = qMin(pcmFrame.size(), m_pcmFrameBytes);
    if (copyBytes > 0)
        std::memcpy(inputSamples.data(), pcmFrame.constData(), copyBytes);
    if (copyBytes < m_pcmFrameBytes)
        std::memset(reinterpret_cast<char*>(inputSamples.data()) + copyBytes, 0,
                    m_pcmFrameBytes - copyBytes);

    QByteArray output(m_frameBytes, 0);
    QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
    m_codec2Encode(m_codec,
                   reinterpret_cast<unsigned char*>(output.data()),
                   inputSamples.data());
    return output;
#else
    return pcmFrame;
#endif
}

QByteArray Codec2Wrapper::decode(const QByteArray& codecFrame) const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_forcePcm || m_pcmFrameBytes <= 0)
        return codecFrame;

#ifdef INCOMUDON_USE_OPUS
    if (m_opusActive && m_codecType == CodecTypeOpus && m_opusDecoder)
    {
        const int expectedSamples = m_pcmFrameBytes / static_cast<int>(sizeof(opus_int16));
        QVector<opus_int16> outputSamples(expectedSamples, 0);
        const int decodedSamples = (m_opusUsingRuntimeApi && m_opusDecode)
            ? m_opusDecode(
                m_opusDecoder,
                codecFrame.isEmpty()
                    ? nullptr
                    : reinterpret_cast<const unsigned char*>(codecFrame.constData()),
                codecFrame.size(),
                outputSamples.data(),
                expectedSamples,
                0)
            : opus_decode(
                m_opusDecoder,
                codecFrame.isEmpty()
                    ? nullptr
                    : reinterpret_cast<const unsigned char*>(codecFrame.constData()),
                codecFrame.size(),
                outputSamples.data(),
                expectedSamples,
                0);

        QByteArray output(m_pcmFrameBytes, 0);
        if (decodedSamples <= 0)
            return output;

        const int copySamples = qMin(decodedSamples, expectedSamples);
        const int copyBytes = copySamples * static_cast<int>(sizeof(opus_int16));
        if (copyBytes > 0)
            std::memcpy(output.data(), outputSamples.constData(), copyBytes);
        return output;
    }
#endif

#ifdef INCOMUDON_USE_CODEC2
    if (!m_codec || !m_codec2Decode || m_frameBytes <= 0)
        return codecFrame;

    QByteArray input = codecFrame;
    if (input.size() < m_frameBytes)
        input.append(QByteArray(m_frameBytes - input.size(), 0));
    else if (input.size() > m_frameBytes)
        input.truncate(m_frameBytes);

    const int samples = m_pcmFrameBytes / static_cast<int>(sizeof(short));
    QVector<short> outputSamples(samples);
    QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
    m_codec2Decode(m_codec,
                   outputSamples.data(),
                   reinterpret_cast<const unsigned char*>(input.constData()));

    QByteArray output(m_pcmFrameBytes, 0);
    std::memcpy(output.data(), outputSamples.data(), m_pcmFrameBytes);
    return output;
#else
    return codecFrame;
#endif
}

void Codec2Wrapper::updateCodec()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);

#ifdef INCOMUDON_USE_CODEC2
    if (m_codec && m_codec2Destroy)
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        m_codec2Destroy(m_codec);
        m_codec = nullptr;
    }
#endif
#ifdef INCOMUDON_USE_OPUS
    if (m_opusEncoder)
    {
        if (m_opusUsingRuntimeApi && m_opusEncoderDestroy)
            m_opusEncoderDestroy(m_opusEncoder);
        else
            opus_encoder_destroy(m_opusEncoder);
        m_opusEncoder = nullptr;
    }
    if (m_opusDecoder)
    {
        if (m_opusUsingRuntimeApi && m_opusDecoderDestroy)
            m_opusDecoderDestroy(m_opusDecoder);
        else
            opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
    m_opusUsingRuntimeApi = false;
#endif

    const bool shouldUseCodec = !m_forcePcm;
    const bool requestOpus = shouldUseCodec && (m_codecType == CodecTypeOpus);
    const bool requestCodec2 = shouldUseCodec && !requestOpus;

#ifdef INCOMUDON_USE_OPUS
    if (requestOpus)
    {
        if (!m_opusLibraryPath.trimmed().isEmpty() && !m_opusLibraryLoaded)
            refreshOpusLibrary();

        const bool useRuntimeApi = m_opusLibraryLoaded &&
                                   m_opusEncoderCreate &&
                                   m_opusDecoderCreate &&
                                   m_opusEncoderDestroy &&
                                   m_opusDecoderDestroy &&
                                   m_opusEncode &&
                                   m_opusDecode &&
                                   m_opusEncoderCtl;
        int encErr = 0;
        int decErr = 0;
        if (useRuntimeApi)
        {
            m_opusEncoder = m_opusEncoderCreate(8000, 1, OPUS_APPLICATION_VOIP, &encErr);
            m_opusDecoder = m_opusDecoderCreate(8000, 1, &decErr);
            m_opusUsingRuntimeApi = true;
        }
        else
        {
            m_opusEncoder = opus_encoder_create(8000, 1, OPUS_APPLICATION_VOIP, &encErr);
            m_opusDecoder = opus_decoder_create(8000, 1, &decErr);
            m_opusUsingRuntimeApi = false;
        }
        if (m_opusEncoder && m_opusDecoder &&
            encErr == OPUS_OK && decErr == OPUS_OK)
        {
            const int bitrate = opusBitrateForMode(m_mode);
            if (m_opusUsingRuntimeApi && m_opusEncoderCtl)
            {
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_BITRATE(bitrate));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_VBR(0));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_DTX(0));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                m_opusEncoderCtl(m_opusEncoder, OPUS_SET_COMPLEXITY(5));
            }
            else
            {
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_BITRATE(bitrate));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_VBR(0));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_DTX(0));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                opus_encoder_ctl(m_opusEncoder, OPUS_SET_COMPLEXITY(5));
            }

            const int targetFrameBytes = qBound(8, bitrate / 400, 512);
            if (m_frameBytes != targetFrameBytes)
            {
                m_frameBytes = targetFrameBytes;
                emit frameBytesChanged();
            }
            if (m_pcmFrameBytes != 320)
            {
                m_pcmFrameBytes = 320;
                emit pcmFrameBytesChanged();
            }
            if (m_frameMs != 20)
            {
                m_frameMs = 20;
                emit frameMsChanged();
            }
            if (!m_opusActive)
            {
                m_opusActive = true;
                emit opusActiveChanged();
            }
            if (m_codec2Active)
            {
                m_codec2Active = false;
                emit codec2ActiveChanged();
            }
            return;
        }

        logCodec2Status("Opus init failed encErr=%d decErr=%d (requested bitrate=%d path=%s loaded=%d).",
                        encErr,
                        decErr,
                        m_mode,
                        m_opusLibraryPath.toUtf8().constData(),
                        m_opusLibraryLoaded ? 1 : 0);
        if (m_opusEncoder)
        {
            if (m_opusUsingRuntimeApi && m_opusEncoderDestroy)
                m_opusEncoderDestroy(m_opusEncoder);
            else
                opus_encoder_destroy(m_opusEncoder);
            m_opusEncoder = nullptr;
        }
        if (m_opusDecoder)
        {
            if (m_opusUsingRuntimeApi && m_opusDecoderDestroy)
                m_opusDecoderDestroy(m_opusDecoder);
            else
                opus_decoder_destroy(m_opusDecoder);
            m_opusDecoder = nullptr;
        }
        m_opusUsingRuntimeApi = false;
    }
#endif

#ifdef INCOMUDON_USE_CODEC2
    int codecMode = kCodec2Mode1600;
    if (requestCodec2)
    {
        switch (m_mode)
        {
        case 450:
            codecMode = kCodec2Mode450;
            break;
        case 700:
            codecMode = kCodec2Mode700C;
            break;
        case 2400:
            codecMode = kCodec2Mode2400;
            break;
        case 3200:
            codecMode = kCodec2Mode3200;
            break;
        default:
            codecMode = kCodec2Mode1600;
            break;
        }
    }

    if (requestCodec2 && !m_codec2LibraryLoaded)
        refreshCodec2Library();

    if (requestCodec2 && m_codec2LibraryLoaded &&
        m_codec2Create && m_codec2BitsPerFrame && m_codec2SamplesPerFrame)
    {
        int bitsPerFrame = 0;
        int samples = 0;
        {
            QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
            m_codec = m_codec2Create(codecMode);
            if (m_codec)
            {
                bitsPerFrame = m_codec2BitsPerFrame(m_codec);
                samples = m_codec2SamplesPerFrame(m_codec);
            }
        }
        if (m_codec)
        {
            if (bitsPerFrame <= 0 || bitsPerFrame > 4096 ||
                samples <= 0 || samples > 4096)
            {
                logCodec2Status("codec2 returned invalid frame params bits=%d samples=%d mode=%d.",
                                bitsPerFrame, samples, m_mode);
                if (m_codec2Destroy)
                {
                    QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                    m_codec2Destroy(m_codec);
                    m_codec = nullptr;
                }
            }
            else
            {
                const int newFrameBytes = (bitsPerFrame + 7) / 8;
                const int newPcmBytes = samples * static_cast<int>(sizeof(short));
                const int newFrameMs = (samples * 1000) / 8000;
                if (newFrameBytes <= 0 || newPcmBytes <= 0)
                {
                    logCodec2Status("codec2 produced zero-sized frames bytes=%d pcm=%d mode=%d.",
                                    newFrameBytes, newPcmBytes, m_mode);
                    if (m_codec2Destroy)
                    {
                        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
                        m_codec2Destroy(m_codec);
                        m_codec = nullptr;
                    }
                }
                else
                {
                    if (!m_codec2Active)
                    {
                        m_codec2Active = true;
                        emit codec2ActiveChanged();
                    }
                    if (m_opusActive)
                    {
                        m_opusActive = false;
                        emit opusActiveChanged();
                    }

                    if (m_frameBytes != newFrameBytes)
                    {
                        m_frameBytes = newFrameBytes;
                        emit frameBytesChanged();
                    }
                    if (m_pcmFrameBytes != newPcmBytes)
                    {
                        m_pcmFrameBytes = newPcmBytes;
                        emit pcmFrameBytesChanged();
                    }
                    if (m_frameMs != newFrameMs && newFrameMs > 0)
                    {
                        m_frameMs = newFrameMs;
                        emit frameMsChanged();
                    }
                    return;
                }
            }
        }
        logCodec2Status("codec2_create failed for mode=%d (requested bitrate=%d).", codecMode, m_mode);
    }

    if (requestCodec2)
    {
        if (!m_codec2LibraryError.isEmpty())
            logCodec2Status("Falling back to PCM: %s", m_codec2LibraryError.toUtf8().constData());
        else
            logCodec2Status("Falling back to PCM mode=%d (requested bitrate=%d).", codecMode, m_mode);
    }
#endif

#ifndef INCOMUDON_USE_OPUS
    if (requestOpus)
        logCodec2Status("Falling back to PCM: Opus support disabled at build time.");
#endif

    if (m_codec2Active)
    {
        m_codec2Active = false;
        emit codec2ActiveChanged();
    }
    if (m_opusActive)
    {
        m_opusActive = false;
        emit opusActiveChanged();
    }
    const int fallbackPcm = 320;
    if (m_pcmFrameBytes != fallbackPcm)
    {
        m_pcmFrameBytes = fallbackPcm;
        emit pcmFrameBytesChanged();
    }
    if (m_frameBytes != m_pcmFrameBytes)
    {
        m_frameBytes = m_pcmFrameBytes;
        emit frameBytesChanged();
    }
    if (m_frameMs != 20)
    {
        m_frameMs = 20;
        emit frameMsChanged();
    }
}

int Codec2Wrapper::normalizeMode(int mode) const
{
    const int options[] = {450, 700, 1600, 2400, 3200};
    int best = options[0];
    int bestDiff = qAbs(mode - options[0]);
    for (int i = 1; i < 5; ++i)
    {
        const int diff = qAbs(mode - options[i]);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = options[i];
        }
    }
    return best;
}

int Codec2Wrapper::opusBitrateForMode(int mode) const
{
    if (mode >= 6000)
    {
        static constexpr int options[] = {6000, 8000, 12000, 16000, 20000, 64000, 96000, 128000};
        int best = options[0];
        int bestDiff = qAbs(mode - options[0]);
        for (int i = 1; i < 8; ++i)
        {
            const int diff = qAbs(mode - options[i]);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                best = options[i];
            }
        }
        return best;
    }

    switch (normalizeMode(mode))
    {
    case 450:
        return 6000;
    case 700:
        return 8000;
    case 2400:
        return 16000;
    case 3200:
        return 20000;
    case 1600:
    default:
        return 12000;
    }
}

#ifdef INCOMUDON_USE_CODEC2
void Codec2Wrapper::unloadCodec2Library()
{
    if (!m_codec2Library)
        return;
    if (m_codec2Library->isLoaded())
        m_codec2Library->unload();
}

void Codec2Wrapper::clearCodec2Api()
{
    m_codec2Create = nullptr;
    m_codec2Destroy = nullptr;
    m_codec2Encode = nullptr;
    m_codec2Decode = nullptr;
    m_codec2BitsPerFrame = nullptr;
    m_codec2SamplesPerFrame = nullptr;
}

QString Codec2Wrapper::normalizeLibraryPath(const QString& path, QString* error) const
{
    return normalizeDynamicLibraryPath(path, error);
}

void Codec2Wrapper::setCodec2LibraryLoadedInternal(bool loaded)
{
    if (m_codec2LibraryLoaded == loaded)
        return;
    m_codec2LibraryLoaded = loaded;
    emit codec2LibraryLoadedChanged();
}

void Codec2Wrapper::setCodec2LibraryErrorInternal(const QString& error)
{
    if (m_codec2LibraryError == error)
        return;
    m_codec2LibraryError = error;
    emit codec2LibraryErrorChanged();
}

void Codec2Wrapper::refreshCodec2Library()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (!m_codec2Library)
        return;

    if (m_codec && m_codec2Destroy)
    {
        QMutexLocker<QRecursiveMutex> apiLocker(&codec2ApiMutex());
        m_codec2Destroy(m_codec);
        m_codec = nullptr;
    }

    clearCodec2Api();
    unloadCodec2Library();

    const bool explicitPath = !m_codec2LibraryPath.trimmed().isEmpty();
    QString normalizedError;
    const QString normalizedPath = normalizeLibraryPath(m_codec2LibraryPath, &normalizedError);
    if (explicitPath && normalizedPath.isEmpty())
    {
        setCodec2LibraryLoadedInternal(false);
        if (normalizedError.isEmpty())
            normalizedError = QStringLiteral("Invalid codec2 library path");
        setCodec2LibraryErrorInternal(normalizedError);
        return;
    }
    QStringList candidates;
    if (explicitPath)
    {
        candidates << normalizedPath;
    }
    else
    {
#if defined(INCOMUDON_CODEC2_RUNTIME_LOADER) && defined(Q_OS_ANDROID)
        setCodec2LibraryLoadedInternal(false);
        setCodec2LibraryErrorInternal(QStringLiteral("Codec2 library path is not set"));
        return;
#else
        candidates << QStringLiteral("codec2") << QStringLiteral("libcodec2");
#endif
    }

    QString lastError;
    for (const QString& candidate : candidates)
    {
        m_codec2Library->setFileName(candidate);
        if (!m_codec2Library->load())
        {
            lastError = m_codec2Library->errorString();
            continue;
        }

        m_codec2Create = reinterpret_cast<Codec2CreateFn>(m_codec2Library->resolve("codec2_create"));
        m_codec2Destroy = reinterpret_cast<Codec2DestroyFn>(m_codec2Library->resolve("codec2_destroy"));
        m_codec2Encode = reinterpret_cast<Codec2EncodeFn>(m_codec2Library->resolve("codec2_encode"));
        m_codec2Decode = reinterpret_cast<Codec2DecodeFn>(m_codec2Library->resolve("codec2_decode"));
        m_codec2BitsPerFrame = reinterpret_cast<Codec2BitsPerFrameFn>(m_codec2Library->resolve("codec2_bits_per_frame"));
        m_codec2SamplesPerFrame = reinterpret_cast<Codec2SamplesPerFrameFn>(m_codec2Library->resolve("codec2_samples_per_frame"));

        const bool symbolsOk = (m_codec2Create && m_codec2Destroy &&
                                m_codec2Encode && m_codec2Decode &&
                                m_codec2BitsPerFrame && m_codec2SamplesPerFrame);
        if (symbolsOk)
        {
            setCodec2LibraryLoadedInternal(true);
            setCodec2LibraryErrorInternal(QString());
            return;
        }

        lastError = QStringLiteral("Required codec2 symbols were not found in: %1")
                        .arg(candidate);
        clearCodec2Api();
        unloadCodec2Library();
    }

    setCodec2LibraryLoadedInternal(false);
    if (explicitPath)
    {
        if (lastError.isEmpty())
            lastError = QStringLiteral("Failed to load codec2 library: %1").arg(normalizedPath);
        setCodec2LibraryErrorInternal(lastError);
    }
    else
    {
        setCodec2LibraryErrorInternal(QString());
    }
}
#endif
