#include "udptransport.h"

UdpTransport::UdpTransport(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead,
            this, &UdpTransport::onReadyRead);
}

bool UdpTransport::bind(quint16 port)
{
    if (!m_socket.bind(QHostAddress::AnyIPv4, port))
    {
        emit bindFailed(m_socket.errorString());
        return false;
    }

    emit bound(m_socket.localPort());
    return true;
}

void UdpTransport::send(const QByteArray& data,
                        const QHostAddress& addr,
                        quint16 port)
{
    m_socket.writeDatagram(data, addr, port);
}

quint16 UdpTransport::localPort() const
{
    return m_socket.localPort();
}

void UdpTransport::onReadyRead()
{
    while (m_socket.hasPendingDatagrams())
    {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_socket.pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket.readDatagram(datagram.data(), datagram.size(),
                              &sender, &senderPort);

        emit datagramReceived(datagram, sender, senderPort);
    }
}
