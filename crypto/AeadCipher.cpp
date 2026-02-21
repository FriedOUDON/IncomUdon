#include "AeadCipher.h"

#include <QCryptographicHash>
#include <cstring>

#ifdef INCOMUDON_USE_OPENSSL
#include <openssl/evp.h>
#endif

namespace {
constexpr int kTagSize = 16;
constexpr int kIvSize = 12;

static void nonceToIv(quint64 nonce, unsigned char iv[kIvSize])
{
    std::memset(iv, 0, kIvSize);
    for (int i = 0; i < 8; ++i)
    {
        iv[kIvSize - 1 - i] = static_cast<unsigned char>((nonce >> (i * 8)) & 0xFF);
    }
}
}

AeadCipher::AeadCipher(QObject* parent)
    : QObject(parent)
{
}

void AeadCipher::setKey(const QByteArray& key, const QByteArray& nonceBase)
{
    if (key.isEmpty())
    {
        if (m_key.isEmpty() && m_nonceBase == 0 && m_nonceCounter == 0)
            return;
        m_key.clear();
        m_nonceBase = 0;
        m_nonceCounter = 0;
        return;
    }

    QByteArray normalizedKey;
    if (key.size() == 32)
        normalizedKey = key;
    else
        normalizedKey = QCryptographicHash::hash(key, QCryptographicHash::Sha256);

    const quint64 newNonceBase = bytesToU64(nonceBase);
    if (m_key == normalizedKey && m_nonceBase == newNonceBase)
        return;

    m_key = normalizedKey;
    m_nonceBase = newNonceBase;
    m_nonceCounter = 0;
}

bool AeadCipher::isReady() const
{
    return !m_key.isEmpty();
}

void AeadCipher::setMode(Mode mode)
{
#ifdef INCOMUDON_USE_OPENSSL
    m_mode = mode;
#else
    Q_UNUSED(mode)
    m_mode = Mode::LegacyXor;
#endif
}

AeadCipher::Mode AeadCipher::mode() const
{
    return m_mode;
}

quint32 AeadCipher::keyId() const
{
    return m_keyId;
}

void AeadCipher::setKeyId(quint32 id)
{
    if (m_keyId == id)
        return;

    m_keyId = id;
    emit keyIdChanged();
}

quint64 AeadCipher::nextNonce()
{
    return m_nonceBase + (m_nonceCounter++);
}

AeadResult AeadCipher::encrypt(const QByteArray& plaintext,
                               quint64 nonce,
                               const QByteArray& aad) const
{
    AeadResult result;
    result.ciphertext = plaintext;
    result.tag = QByteArray(kTagSize, 0);

#ifdef INCOMUDON_USE_OPENSSL
    if (m_mode == Mode::AesGcm)
    {
    if (m_key.isEmpty())
        return result;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return result;

    unsigned char iv[kIvSize];
    nonceToIv(nonce, iv);

    int len = 0;
    int outLen = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(m_key.constData()),
                           iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    if (!aad.isEmpty())
    {
        if (EVP_EncryptUpdate(ctx, nullptr, &len,
                              reinterpret_cast<const unsigned char*>(aad.constData()),
                              aad.size()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }
    }

    result.ciphertext.resize(plaintext.size());
    if (EVP_EncryptUpdate(ctx,
                          reinterpret_cast<unsigned char*>(result.ciphertext.data()),
                          &len,
                          reinterpret_cast<const unsigned char*>(plaintext.constData()),
                          plaintext.size()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }
    outLen = len;

    if (EVP_EncryptFinal_ex(ctx,
                            reinterpret_cast<unsigned char*>(result.ciphertext.data()) + outLen,
                            &len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }
    outLen += len;
    result.ciphertext.resize(outLen);

    result.tag.resize(kTagSize);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                            kTagSize, result.tag.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return result;
    }

    EVP_CIPHER_CTX_free(ctx);
    return result;
    }
#endif
    if (!m_key.isEmpty())
    {
        for (int i = 0; i < result.ciphertext.size(); ++i)
        {
            const char keyByte = m_key[i % m_key.size()];
            result.ciphertext[i] = static_cast<char>(result.ciphertext[i] ^ keyByte);
        }
    }

    result.tag = computeTag(result.ciphertext, nonce, aad);
    return result;
}

bool AeadCipher::decrypt(const QByteArray& ciphertext,
                         const QByteArray& tag,
                         quint64 nonce,
                         const QByteArray& aad,
                         QByteArray& plaintextOut) const
{
#ifdef INCOMUDON_USE_OPENSSL
    if (m_mode == Mode::AesGcm)
    {
    if (m_key.isEmpty() || tag.size() != kTagSize)
        return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    unsigned char iv[kIvSize];
    nonceToIv(nonce, iv);

    int len = 0;
    int outLen = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(m_key.constData()),
                           iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (!aad.isEmpty())
    {
        if (EVP_DecryptUpdate(ctx, nullptr, &len,
                              reinterpret_cast<const unsigned char*>(aad.constData()),
                              aad.size()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
    }

    plaintextOut.resize(ciphertext.size());
    if (EVP_DecryptUpdate(ctx,
                          reinterpret_cast<unsigned char*>(plaintextOut.data()),
                          &len,
                          reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                          ciphertext.size()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    outLen = len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                            kTagSize, const_cast<char*>(tag.constData())) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    const int finalOk = EVP_DecryptFinal_ex(ctx,
                                           reinterpret_cast<unsigned char*>(plaintextOut.data()) + outLen,
                                           &len);
    EVP_CIPHER_CTX_free(ctx);
    if (finalOk != 1)
        return false;

    outLen += len;
    plaintextOut.resize(outLen);
    return true;
    }
#endif
    const QByteArray expected = computeTag(ciphertext, nonce, aad);
    if (expected != tag)
        return false;

    plaintextOut = ciphertext;
    if (!m_key.isEmpty())
    {
        for (int i = 0; i < plaintextOut.size(); ++i)
        {
            const char keyByte = m_key[i % m_key.size()];
            plaintextOut[i] = static_cast<char>(plaintextOut[i] ^ keyByte);
        }
    }

    return true;
}

quint64 AeadCipher::bytesToU64(const QByteArray& bytes)
{
    quint64 value = 0;
    const int count = qMin(bytes.size(), 8);
    for (int i = 0; i < count; ++i)
    {
        value = (value << 8) | static_cast<quint8>(bytes[i]);
    }
    return value;
}

QByteArray AeadCipher::computeTag(const QByteArray& ciphertext,
                                  quint64 nonce,
                                  const QByteArray& aad) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(m_key);
    hash.addData(aad);
    hash.addData(ciphertext);
    hash.addData(reinterpret_cast<const char*>(&nonce), sizeof(nonce));
    return hash.result().left(16);
}
