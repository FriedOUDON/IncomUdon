#include "udptransport.h"
#include <QVariant>

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

    applyQosOption();
    emit bound(m_socket.localPort());
    return true;
}

bool UdpTransport::qosEnabled() const
{
    return m_qosEnabled;
}

void UdpTransport::setQosEnabled(bool enabled)
{
    if (m_qosEnabled == enabled)
        return;

    m_qosEnabled = enabled;
    applyQosOption();
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

void UdpTransport::applyQosOption()
{
    // DSCP EF (46) for voice; lower 2 bits are ECN.
    const int tos = m_qosEnabled ? (46 << 2) : 0;
    m_socket.setSocketOption(QAbstractSocket::TypeOfServiceOption, QVariant(tos));
}
