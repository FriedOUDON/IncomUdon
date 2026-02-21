#pragma once

#include <QObject>
#include <QByteArray>

#include "packet.h"

struct ParsedPacket
{
    Proto::PacketHeader header;
    Proto::SecurityHeader sec;
    QByteArray encryptedPayload;
    QByteArray authTag;
};

class Packetizer : public QObject
{
    Q_OBJECT

public:
    explicit Packetizer(QObject* parent = nullptr);

    void setChannelId(quint32 channelId);
    void setSenderId(quint32 senderId);
    void setKeyId(quint32 keyId);

    quint32 channelId() const;
    quint32 senderId() const;
    quint32 keyId() const;

    void setUseLegacy(bool legacy);
    bool useLegacy() const;
    QByteArray pack(Proto::PacketType type,
                    const QByteArray& encryptedPayload,
                    const QByteArray& authTag,
                    quint64 nonce);

    QByteArray packPlain(Proto::PacketType type,
                         const QByteArray& payload);

    QByteArray packLegacy(Proto::PacketType type,
                          const QByteArray& encryptedPayload,
                          const QByteArray& authTag,
                          quint64 nonce);

    QByteArray packPlainLegacy(Proto::PacketType type,
                               const QByteArray& payload);

    bool unpack(const QByteArray& datagram, ParsedPacket& out) const;

    quint16 nextSeq() const;

private:
    quint32 m_channelId = 0;
    quint32 m_senderId = 0;
    quint32 m_keyId = 0;
    quint16 m_seq = 0;
    bool m_useLegacy = false;
};
