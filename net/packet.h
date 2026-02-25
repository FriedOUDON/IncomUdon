#pragma once

#include <QtGlobal>
#include <QByteArray>

namespace Proto {

static constexpr quint8  PROTOCOL_VERSION = 1;
static constexpr quint16 FIXED_HEADER_SIZE = 16;
static constexpr quint16 LEGACY_FIXED_HEADER_SIZE = 14;
static constexpr quint16 SECURITY_HEADER_SIZE = 12;
static constexpr quint16 AUTH_TAG_SIZE = 16;

enum PacketType : quint8 {
    PKT_AUDIO     = 0x01,
    PKT_PTT_ON    = 0x02,
    PKT_PTT_OFF   = 0x03,
    PKT_KEEPALIVE = 0x04,
    PKT_JOIN      = 0x05,
    PKT_LEAVE     = 0x06,
    PKT_TALK_GRANT   = 0x07,
    PKT_TALK_RELEASE = 0x08,
    PKT_TALK_DENY    = 0x09,
    PKT_KEY_EXCHANGE = 0x0A,
    PKT_CODEC_CONFIG = 0x0B,
    PKT_FEC          = 0x0C,
    PKT_SERVER_CONFIG = 0x0D
};

enum CodecTransportId : quint8 {
    CODEC_TRANSPORT_PCM = 0x00,
    CODEC_TRANSPORT_CODEC2 = 0x01,
    CODEC_TRANSPORT_OPUS = 0x02
};

struct PacketHeader {
    quint8  version;
    quint8  type;
    quint16 headerLen;
    quint32 channelId;
    quint32 senderId;
    quint16 seq;
    quint16 flags;
};

struct SecurityHeader {
    quint64 nonce;
    quint32 keyId;
};

QByteArray serializePacket(const PacketHeader& header,
                           const SecurityHeader& sec,
                           const QByteArray& encryptedPayload,
                           const QByteArray& authTag);

bool deserializePacket(const QByteArray& datagram,
                       PacketHeader& header,
                       SecurityHeader& sec,
                       QByteArray& encryptedPayload,
                       QByteArray& authTag);

} // namespace Proto
