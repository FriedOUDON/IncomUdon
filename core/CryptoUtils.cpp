#include "CryptoUtils.h"

#include <QCryptographicHash>

namespace {
bool isHexDigit(QChar c)
{
    const ushort u = c.unicode();
    return (u >= u'0' && u <= u'9') ||
           (u >= u'a' && u <= u'f') ||
           (u >= u'A' && u <= u'F');
}
}

CryptoUtils::CryptoUtils(QObject* parent)
    : QObject(parent)
{
}

QString CryptoUtils::sha256Hex(const QString& text) const
{
    const QByteArray digest = QCryptographicHash::hash(text.toUtf8(),
                                                       QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

bool CryptoUtils::isSha256Hex(const QString& text) const
{
    if (text.size() != 64)
        return false;

    for (const QChar c : text)
    {
        if (!isHexDigit(c))
            return false;
    }
    return true;
}

QString CryptoUtils::normalizePasswordHashHex(const QString& text) const
{
    if (text.isEmpty())
        return QString();

    if (isSha256Hex(text))
        return text.toLower();

    return sha256Hex(text);
}

