#pragma once

#include <QObject>
#include <QByteArray>
#include <QRecursiveMutex>
#include <QString>

#ifdef INCOMUDON_USE_CODEC2
struct CODEC2;
#endif

class Codec2Wrapper : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int mode
               READ mode
               WRITE setMode
               NOTIFY modeChanged)
    Q_PROPERTY(int frameBytes
               READ frameBytes
               WRITE setFrameBytes
               NOTIFY frameBytesChanged)
    Q_PROPERTY(int pcmFrameBytes
               READ pcmFrameBytes
               NOTIFY pcmFrameBytesChanged)
    Q_PROPERTY(int frameMs
               READ frameMs
               NOTIFY frameMsChanged)
    Q_PROPERTY(bool forcePcm
               READ forcePcm
               WRITE setForcePcm
               NOTIFY forcePcmChanged)
    Q_PROPERTY(bool codec2Active
               READ codec2Active
               NOTIFY codec2ActiveChanged)
    Q_PROPERTY(QString codec2LibraryPath
               READ codec2LibraryPath
               WRITE setCodec2LibraryPath
               NOTIFY codec2LibraryPathChanged)
    Q_PROPERTY(bool codec2LibraryLoaded
               READ codec2LibraryLoaded
               NOTIFY codec2LibraryLoadedChanged)
    Q_PROPERTY(QString codec2LibraryError
               READ codec2LibraryError
               NOTIFY codec2LibraryErrorChanged)

public:
    explicit Codec2Wrapper(QObject* parent = nullptr);
    ~Codec2Wrapper() override;

    int mode() const;
    void setMode(int mode);

    int frameBytes() const;
    void setFrameBytes(int bytes);

    int pcmFrameBytes() const;
    int frameMs() const;
    bool forcePcm() const;
    void setForcePcm(bool force);
    bool codec2Active() const;
    QString codec2LibraryPath() const;
    void setCodec2LibraryPath(const QString& path);
    bool codec2LibraryLoaded() const;
    QString codec2LibraryError() const;

    QByteArray encode(const QByteArray& pcmFrame) const;
    QByteArray decode(const QByteArray& codecFrame) const;

signals:
    void modeChanged();
    void frameBytesChanged();
    void pcmFrameBytesChanged();
    void frameMsChanged();
    void forcePcmChanged();
    void codec2ActiveChanged();
    void codec2LibraryPathChanged();
    void codec2LibraryLoadedChanged();
    void codec2LibraryErrorChanged();

private:
    void updateCodec();
    int normalizeMode(int mode) const;

    mutable QRecursiveMutex m_mutex;
    int m_mode = 1600;
    int m_frameBytes = 160;
    int m_pcmFrameBytes = 320;
    int m_frameMs = 20;
    bool m_forcePcm = false;
    bool m_codec2Active = false;
    QString m_codec2LibraryPath;
    bool m_codec2LibraryLoaded = false;
    QString m_codec2LibraryError;

#ifdef INCOMUDON_USE_CODEC2
    typedef CODEC2* (*Codec2CreateFn)(int);
    typedef void (*Codec2DestroyFn)(CODEC2*);
    typedef void (*Codec2EncodeFn)(CODEC2*, unsigned char*, short*);
    typedef void (*Codec2DecodeFn)(CODEC2*, short*, const unsigned char*);
    typedef int (*Codec2BitsPerFrameFn)(CODEC2*);
    typedef int (*Codec2SamplesPerFrameFn)(CODEC2*);

    void unloadCodec2Library();
    void clearCodec2Api();
    QString normalizeLibraryPath(const QString& path, QString* error = nullptr) const;
    void refreshCodec2Library();
    void setCodec2LibraryLoadedInternal(bool loaded);
    void setCodec2LibraryErrorInternal(const QString& error);

    mutable struct CODEC2* m_codec = nullptr;
    Codec2CreateFn m_codec2Create = nullptr;
    Codec2DestroyFn m_codec2Destroy = nullptr;
    Codec2EncodeFn m_codec2Encode = nullptr;
    Codec2DecodeFn m_codec2Decode = nullptr;
    Codec2BitsPerFrameFn m_codec2BitsPerFrame = nullptr;
    Codec2SamplesPerFrameFn m_codec2SamplesPerFrame = nullptr;
#endif

#ifdef INCOMUDON_USE_CODEC2
    class QLibrary* m_codec2Library = nullptr;
#endif
};
