#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QtGlobal>

class KeyExchange : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool ready
               READ isReady
               NOTIFY readyChanged)

public:
    enum CryptoMode {
        AesGcm = 0,
        LegacyXor = 1
    };
    Q_ENUM(CryptoMode)

    explicit KeyExchange(QObject* parent = nullptr);

    bool isReady() const;

    void setChannelId(quint32 channelId);
    void setPassword(const QString& password);

    QByteArray passwordKey() const;
    CryptoMode cryptoMode() const;
    void setPreferredMode(CryptoMode mode);
    CryptoMode preferredMode() const;

public slots:
    void startHandshake();
    void processHandshakePacket(const QByteArray& packet);

signals:
    void sessionKeyReady(const QByteArray& key,
                         const QByteArray& nonceBase,
                         CryptoMode mode);
    void handshakePacketReady(const QByteArray& packet);
    void readyChanged();

private:
    QByteArray derivePasswordKey() const;

    bool m_ready = false;
    CryptoMode m_cryptoMode =
#ifdef INCOMUDON_USE_OPENSSL
        CryptoMode::AesGcm;
#else
        CryptoMode::LegacyXor;
#endif
    CryptoMode m_preferredMode = m_cryptoMode;
    quint32 m_channelId = 0;
    QString m_password;
    QByteArray m_passwordKey;
};
