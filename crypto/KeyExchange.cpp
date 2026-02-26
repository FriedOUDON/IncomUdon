#include "KeyExchange.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QTimer>

namespace {
QByteArray hkdfSha256(const QByteArray& ikm,
                      const QByteArray& salt,
                      const QByteArray& info,
                      int length)
{
    QByteArray prk = QMessageAuthenticationCode::hash(ikm, salt, QCryptographicHash::Sha256);

    QByteArray okm;
    QByteArray t;
    int counter = 1;
    while (okm.size() < length)
    {
        QByteArray data = t + info + QByteArray(1, static_cast<char>(counter));
        t = QMessageAuthenticationCode::hash(data, prk, QCryptographicHash::Sha256);
        okm.append(t);
        counter++;
    }

    okm.truncate(length);
    return okm;
}

QByteArray randomNonceBase()
{
    const quint64 value = QRandomGenerator::global()->generate64();
    QByteArray out(8, 0);
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<char>((value >> ((7 - i) * 8)) & 0xFF);
    return out;
}

bool isSha256HexString(const QString& text)
{
    if (text.size() != 64)
        return false;

    for (const QChar c : text)
    {
        const ushort u = c.unicode();
        const bool isHex = (u >= u'0' && u <= u'9') ||
                           (u >= u'a' && u <= u'f') ||
                           (u >= u'A' && u <= u'F');
        if (!isHex)
            return false;
    }

    return true;
}
}

KeyExchange::KeyExchange(QObject* parent)
    : QObject(parent)
{
}

bool KeyExchange::isReady() const
{
    return m_ready;
}

void KeyExchange::setChannelId(quint32 channelId)
{
    if (m_channelId == channelId)
        return;

    m_channelId = channelId;
    m_passwordKey.clear();
}

void KeyExchange::setPassword(const QString& password)
{
    const QByteArray normalizedHash = normalizePasswordHash(password);
    if (m_passwordHash == normalizedHash)
        return;

    m_passwordHash = normalizedHash;
    m_passwordKey.clear();
}

QByteArray KeyExchange::passwordKey() const
{
    if (m_passwordKey.isEmpty())
        return derivePasswordKey();

    return m_passwordKey;
}

KeyExchange::CryptoMode KeyExchange::cryptoMode() const
{
    return m_cryptoMode;
}

void KeyExchange::setPreferredMode(CryptoMode mode)
{
#ifdef INCOMUDON_USE_OPENSSL
    m_preferredMode = mode;
#else
    Q_UNUSED(mode)
    m_preferredMode = CryptoMode::LegacyXor;
#endif
}

KeyExchange::CryptoMode KeyExchange::preferredMode() const
{
    return m_preferredMode;
}

void KeyExchange::startHandshake()
{
    if (m_ready)
    {
        m_ready = false;
        emit readyChanged();
    }

    if (m_passwordKey.isEmpty())
        m_passwordKey = derivePasswordKey();

#ifdef INCOMUDON_USE_OPENSSL
    if (m_preferredMode == CryptoMode::LegacyXor)
    {
        if (m_ready)
            return;
        const QByteArray ikm = passwordKey();
        const QByteArray info = QByteArrayLiteral("incomudon-session");
        const QByteArray okm = hkdfSha256(ikm, QByteArray(), info, 40);

        const QByteArray key = okm.left(32);
        const QByteArray nonceBase = okm.mid(32, 8);

        m_ready = true;
        m_cryptoMode = CryptoMode::LegacyXor;
        emit readyChanged();

        QTimer::singleShot(0, this, [this, key, nonceBase]() {
            emit sessionKeyReady(key, nonceBase, CryptoMode::LegacyXor);
        });

        emit handshakePacketReady(QByteArrayLiteral("LEGACY"));
        return;
    }

    // Group relay requires a shared room key. Pairwise ECDH cannot work with
    // one encrypted broadcast stream.
    const QByteArray ikm = passwordKey();
    const QByteArray info = QByteArrayLiteral("incomudon-session-aesgcm");
    const QByteArray key = hkdfSha256(ikm, QByteArray(), info, 32);
    const QByteArray nonceBase = randomNonceBase();

    m_ready = true;
    m_cryptoMode = CryptoMode::AesGcm;
    emit readyChanged();

    QTimer::singleShot(0, this, [this, key, nonceBase]() {
        emit sessionKeyReady(key, nonceBase, CryptoMode::AesGcm);
    });
    return;
#else
    // Fallback: derive a deterministic session key from the password so
    // platforms without OpenSSL can still interoperate.
    const QByteArray ikm = passwordKey();
    const QByteArray info = QByteArrayLiteral("incomudon-session");
    const QByteArray okm = hkdfSha256(ikm, QByteArray(), info, 40);

    const QByteArray key = okm.left(32);
    const QByteArray nonceBase = okm.mid(32, 8);

    m_ready = true;
    m_cryptoMode = CryptoMode::LegacyXor;
    emit readyChanged();

    QTimer::singleShot(0, this, [this, key, nonceBase]() {
        emit sessionKeyReady(key, nonceBase, CryptoMode::LegacyXor);
    });

    emit handshakePacketReady(QByteArrayLiteral("LEGACY"));
#endif
}

void KeyExchange::processHandshakePacket(const QByteArray& packet)
{
#ifdef INCOMUDON_USE_OPENSSL
    Q_UNUSED(packet)

    if (m_preferredMode == CryptoMode::LegacyXor)
    {
        // Legacy mode uses a deterministic channel key, so additional
        // handshake packets must not mutate state after readiness.
        if (m_ready && m_cryptoMode == CryptoMode::LegacyXor)
            return;
        const QByteArray ikm = passwordKey();
        const QByteArray info = QByteArrayLiteral("incomudon-session");
        const QByteArray okm = hkdfSha256(ikm, QByteArray(), info, 40);
        const QByteArray sessionKey = okm.left(32);
        const QByteArray nonceBase = okm.mid(32, 8);

        m_ready = true;
        m_cryptoMode = CryptoMode::LegacyXor;
        emit readyChanged();
        emit sessionKeyReady(sessionKey, nonceBase, CryptoMode::LegacyXor);
        emit handshakePacketReady(QByteArrayLiteral("LEGACY"));
        return;
    }

    if (m_ready && m_cryptoMode == CryptoMode::AesGcm)
        return;

    const QByteArray ikm = passwordKey();
    const QByteArray info = QByteArrayLiteral("incomudon-session-aesgcm");
    const QByteArray sessionKey = hkdfSha256(ikm, QByteArray(), info, 32);
    const QByteArray nonceBase = randomNonceBase();

    m_ready = true;
    m_cryptoMode = CryptoMode::AesGcm;
    emit readyChanged();
    emit sessionKeyReady(sessionKey, nonceBase, CryptoMode::AesGcm);
#else
    Q_UNUSED(packet)
#endif
}

QByteArray KeyExchange::derivePasswordKey() const
{
    if (m_passwordHash.isEmpty())
        return QByteArray();

    QByteArray salt;
    salt.append(static_cast<char>((m_channelId >> 24) & 0xFF));
    salt.append(static_cast<char>((m_channelId >> 16) & 0xFF));
    salt.append(static_cast<char>((m_channelId >> 8) & 0xFF));
    salt.append(static_cast<char>(m_channelId & 0xFF));

    QByteArray input = m_passwordHash;
    input.append(salt);

    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}

QByteArray KeyExchange::normalizePasswordHash(const QString& passwordOrHash) const
{
    if (passwordOrHash.isEmpty())
        return QByteArray();

    if (isSha256HexString(passwordOrHash))
        return QByteArray::fromHex(passwordOrHash.toLatin1());

    return QCryptographicHash::hash(passwordOrHash.toUtf8(),
                                    QCryptographicHash::Sha256);
}
