#pragma once

#include <QObject>
#include <QByteArray>
#include <QtGlobal>

struct AeadResult
{
    QByteArray ciphertext;
    QByteArray tag;
};

class AeadCipher : public QObject
{
    Q_OBJECT

    Q_PROPERTY(quint32 keyId
               READ keyId
               WRITE setKeyId
               NOTIFY keyIdChanged)

public:
    enum Mode {
        AesGcm = 0,
        LegacyXor = 1
    };
    Q_ENUM(Mode)

    explicit AeadCipher(QObject* parent = nullptr);

    void setKey(const QByteArray& key, const QByteArray& nonceBase);
    bool isReady() const;

    void setMode(Mode mode);
    Mode mode() const;

    quint32 keyId() const;
    void setKeyId(quint32 id);

    quint64 nextNonce();

    AeadResult encrypt(const QByteArray& plaintext,
                       quint64 nonce,
                       const QByteArray& aad = QByteArray()) const;

    bool decrypt(const QByteArray& ciphertext,
                 const QByteArray& tag,
                 quint64 nonce,
                 const QByteArray& aad,
                 QByteArray& plaintextOut) const;

signals:
    void keyIdChanged();

private:
    static quint64 bytesToU64(const QByteArray& bytes);
    QByteArray computeTag(const QByteArray& ciphertext,
                          quint64 nonce,
                          const QByteArray& aad) const;

    QByteArray m_key;
    quint64 m_nonceBase = 0;
    quint64 m_nonceCounter = 0;
    quint32 m_keyId = 1;
    Mode m_mode =
#ifdef INCOMUDON_USE_OPENSSL
        Mode::AesGcm;
#else
        Mode::LegacyXor;
#endif
};
