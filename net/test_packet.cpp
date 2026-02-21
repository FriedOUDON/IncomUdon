#include "packet.h"
#include <QDebug>

using namespace Proto;

void testPacket()
{
    PacketHeader header {
        PROTOCOL_VERSION,
        PKT_AUDIO,
        FIXED_HEADER_SIZE + SECURITY_HEADER_SIZE,
        1234,
        5678,
        42,
        0
    };

    SecurityHeader sec {
        999999,
        1
    };

    QByteArray payload("HELLO_CODEC2");
    QByteArray tag(AUTH_TAG_SIZE, 0xAA);

    QByteArray serialized = serializePacket(header, sec, payload, tag);

    PacketHeader header2;
    SecurityHeader sec2;
    QByteArray payload2;
    QByteArray tag2;

    bool ok = deserializePacket(serialized,
                                header2,
                                sec2,
                                payload2,
                                tag2);

    qDebug() << "Deserialize OK:" << ok;
    qDebug() << header2.channelId
             << header2.senderId
             << header2.seq;

    qDebug() << payload2;
    qDebug() << tag2.size();
}
