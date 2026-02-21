#include "Packetizer.h"

#include <QtEndian>

static void writeU16(QByteArray& buf, quint16 value)
{
    quint16 be = qToBigEndian(value);
    buf.append(reinterpret_cast<const char*>(&be), sizeof(be));
}

static void writeU32(QByteArray& buf, quint32 value)
{
    quint32 be = qToBigEndian(value);
    buf.append(reinterpret_cast<const char*>(&be), sizeof(be));
}

static void writeU64(QByteArray& buf, quint64 value)
{
    quint64 be = qToBigEndian(value);
    buf.append(reinterpret_cast<const char*>(&be), sizeof(be));
}

Packetizer::Packetizer(QObject* parent)
    : QObject(parent)
{
}

void Packetizer::setChannelId(quint32 channelId)
{
    m_channelId = channelId;
}

void Packetizer::setSenderId(quint32 senderId)
{
    m_senderId = senderId;
}

void Packetizer::setKeyId(quint32 keyId)
{
    m_keyId = keyId;
}

quint32 Packetizer::channelId() const
{
    return m_channelId;
}

quint32 Packetizer::senderId() const
{
    return m_senderId;
}

quint32 Packetizer::keyId() const
{
    return m_keyId;
}

void Packetizer::setUseLegacy(bool legacy)
{
    m_useLegacy = legacy;
}

bool Packetizer::useLegacy() const
{
    return m_useLegacy;
}

QByteArray Packetizer::pack(Proto::PacketType type,
                            const QByteArray& encryptedPayload,
                            const QByteArray& authTag,
                            quint64 nonce)
{
    if (m_useLegacy)
        return packLegacy(type, encryptedPayload, authTag, nonce);

    Proto::PacketHeader header {};
    header.version = Proto::PROTOCOL_VERSION;
    header.type = static_cast<quint8>(type);
    header.headerLen = Proto::FIXED_HEADER_SIZE + Proto::SECURITY_HEADER_SIZE;
    header.channelId = m_channelId;
    header.senderId = m_senderId;
    header.seq = m_seq++;
    header.flags = 0;

    Proto::SecurityHeader sec {};
    sec.nonce = nonce;
    sec.keyId = m_keyId;

    return Proto::serializePacket(header, sec, encryptedPayload, authTag);
}

QByteArray Packetizer::packPlain(Proto::PacketType type, const QByteArray& payload)
{
    if (m_useLegacy)
        return packPlainLegacy(type, payload);

    Proto::SecurityHeader sec {};
    sec.nonce = 0;
    sec.keyId = 0;

    Proto::PacketHeader header {};
    header.version = Proto::PROTOCOL_VERSION;
    header.type = static_cast<quint8>(type);
    header.headerLen = Proto::FIXED_HEADER_SIZE + Proto::SECURITY_HEADER_SIZE;
    header.channelId = m_channelId;
    header.senderId = m_senderId;
    header.seq = m_seq++;
    header.flags = 0;

    QByteArray tag(Proto::AUTH_TAG_SIZE, 0);
    return Proto::serializePacket(header, sec, payload, tag);
}

QByteArray Packetizer::packLegacy(Proto::PacketType type,
                                  const QByteArray& encryptedPayload,
                                  const QByteArray& authTag,
                                  quint64 nonce)
{
    QByteArray buffer;
    buffer.reserve(Proto::LEGACY_FIXED_HEADER_SIZE +
                   Proto::SECURITY_HEADER_SIZE +
                   encryptedPayload.size() +
                   authTag.size());

    buffer.append(static_cast<char>(Proto::PROTOCOL_VERSION));
    buffer.append(static_cast<char>(type));
    writeU16(buffer, Proto::LEGACY_FIXED_HEADER_SIZE + Proto::SECURITY_HEADER_SIZE);
    writeU32(buffer, m_channelId);
    writeU32(buffer, m_senderId);
    writeU16(buffer, m_seq++);

    writeU64(buffer, nonce);
    writeU32(buffer, m_keyId);

    buffer.append(encryptedPayload);
    buffer.append(authTag);

    return buffer;
}

QByteArray Packetizer::packPlainLegacy(Proto::PacketType type, const QByteArray& payload)
{
    QByteArray buffer;
    buffer.reserve(Proto::LEGACY_FIXED_HEADER_SIZE + payload.size());

    buffer.append(static_cast<char>(Proto::PROTOCOL_VERSION));
    buffer.append(static_cast<char>(type));
    writeU16(buffer, Proto::LEGACY_FIXED_HEADER_SIZE);
    writeU32(buffer, m_channelId);
    writeU32(buffer, m_senderId);
    writeU16(buffer, m_seq++);

    buffer.append(payload);
    return buffer;
}

bool Packetizer::unpack(const QByteArray& datagram, ParsedPacket& out) const
{
    return Proto::deserializePacket(datagram,
                                    out.header,
                                    out.sec,
                                    out.encryptedPayload,
                                    out.authTag);
}

quint16 Packetizer::nextSeq() const
{
    return m_seq;
}
