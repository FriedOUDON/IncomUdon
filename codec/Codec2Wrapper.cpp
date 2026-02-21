#include "Codec2Wrapper.h"

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

#if defined(Q_OS_ANDROID)
#include <android/log.h>
#include <QJniEnvironment>
#include <QJniObject>
#endif

#ifdef INCOMUDON_USE_CODEC2
#include "codec2.h"
#endif

namespace
{
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
#ifdef INCOMUDON_USE_CODEC2
    if (m_forcePcm || !m_codec || !m_codec2Encode || m_frameBytes <= 0 || m_pcmFrameBytes <= 0)
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
#ifdef INCOMUDON_USE_CODEC2
    if (m_forcePcm || !m_codec || !m_codec2Decode || m_frameBytes <= 0 || m_pcmFrameBytes <= 0)
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

    bool shouldUseCodec2 = !m_forcePcm;
    int codecMode = CODEC2_MODE_1600;
    if (shouldUseCodec2)
    {
        switch (m_mode)
        {
        case 450:
            codecMode = CODEC2_MODE_450;
            break;
        case 700:
            codecMode = CODEC2_MODE_700C;
            break;
        case 2400:
            codecMode = CODEC2_MODE_2400;
            break;
        case 3200:
            codecMode = CODEC2_MODE_3200;
            break;
        default:
            codecMode = CODEC2_MODE_1600;
            break;
        }
    }

    if (shouldUseCodec2 && !m_codec2LibraryLoaded)
        refreshCodec2Library();

    if (shouldUseCodec2 && m_codec2LibraryLoaded &&
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
#endif

#ifdef INCOMUDON_USE_CODEC2
    if (!m_forcePcm)
    {
        if (!m_codec2LibraryError.isEmpty())
            logCodec2Status("Falling back to PCM: %s", m_codec2LibraryError.toUtf8().constData());
        else
            logCodec2Status("Falling back to PCM mode=%d (requested bitrate=%d).", codecMode, m_mode);
    }
#endif

    if (m_codec2Active)
    {
        m_codec2Active = false;
        emit codec2ActiveChanged();
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
