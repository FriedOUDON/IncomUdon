#include "packet.h"
#include <QtEndian>

namespace Proto {

static void writeUint16(QByteArray& buf, quint16 value)
{
    quint16 be = qToBigEndian(value);
    buf.append(reinterpret_cast<const char*>(&be), sizeof(be));
}

static void writeUint32(QByteArray& buf, quint32 value)
{
    quint32 be = qToBigEndian(value);
    buf.append(reinterpret_cast<const char*>(&be), sizeof(be));
}

static void writeUint64(QByteArray& buf, quint64 value)
{
    quint64 be = qToBigEndian(value);
    buf.append(reinterpret_cast<const char*>(&be), sizeof(be));
}

QByteArray serializePacket(const PacketHeader& header,
                           const SecurityHeader& sec,
                           const QByteArray& encryptedPayload,
                           const QByteArray& authTag)
{
    QByteArray buffer;
    buffer.reserve(FIXED_HEADER_SIZE +
                   SECURITY_HEADER_SIZE +
                   encryptedPayload.size() +
                   authTag.size());

    // ----- Fixed Header -----
    buffer.append(static_cast<char>(header.version));
    buffer.append(static_cast<char>(header.type));
    writeUint16(buffer, header.headerLen);
    writeUint32(buffer, header.channelId);
    writeUint32(buffer, header.senderId);
    writeUint16(buffer, header.seq);
    writeUint16(buffer, header.flags);

    // ----- Security Header -----
    writeUint64(buffer, sec.nonce);
    writeUint32(buffer, sec.keyId);

    // ----- Encrypted Payload -----
    buffer.append(encryptedPayload);

    // ----- Auth Tag -----
    buffer.append(authTag);

    return buffer;
}

static quint16 readUint16(const char* data)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data));
}

static quint32 readUint32(const char* data)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data));
}

static quint64 readUint64(const char* data)
{
    return qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(data));
}

bool deserializePacket(const QByteArray& datagram,
                       PacketHeader& header,
                       SecurityHeader& sec,
                       QByteArray& encryptedPayload,
                       QByteArray& authTag)
{
    if (datagram.size() < LEGACY_FIXED_HEADER_SIZE)
        return false;

    const char* ptr = datagram.constData();
    int offset = 0;

    header.version   = static_cast<quint8>(ptr[offset++]);
    header.type      = static_cast<quint8>(ptr[offset++]);
    header.headerLen = readUint16(ptr + offset); offset += 2;
    header.channelId = readUint32(ptr + offset); offset += 4;
    header.senderId  = readUint32(ptr + offset); offset += 4;
    header.seq       = readUint16(ptr + offset); offset += 2;

    const int tagSize = AUTH_TAG_SIZE;
    int fixedHeaderSizeUsed = LEGACY_FIXED_HEADER_SIZE;

    if (header.headerLen == FIXED_HEADER_SIZE ||
        header.headerLen == FIXED_HEADER_SIZE + SECURITY_HEADER_SIZE)
    {
        if (datagram.size() < FIXED_HEADER_SIZE)
            return false;

        header.flags = readUint16(ptr + offset); offset += 2;
        fixedHeaderSizeUsed = FIXED_HEADER_SIZE;
    }
    else
    {
        header.flags = 0;
        fixedHeaderSizeUsed = LEGACY_FIXED_HEADER_SIZE;
    }

    if (header.headerLen >= fixedHeaderSizeUsed + SECURITY_HEADER_SIZE &&
        datagram.size() >= fixedHeaderSizeUsed + SECURITY_HEADER_SIZE + tagSize)
    {
        sec.nonce = readUint64(ptr + offset); offset += 8;
        sec.keyId = readUint32(ptr + offset); offset += 4;

        int payloadSize = datagram.size() - offset - tagSize;
        if (payloadSize < 0)
            return false;

        encryptedPayload = QByteArray(ptr + offset, payloadSize);
        offset += payloadSize;

        authTag = QByteArray(ptr + offset, tagSize);
        return true;
    }

    if (header.headerLen != fixedHeaderSizeUsed)
        return false;

    sec.nonce = 0;
    sec.keyId = 0;
    encryptedPayload = QByteArray(ptr + offset, datagram.size() - offset);
    authTag.clear();
    return true;
}

} // namespace Proto
