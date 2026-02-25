#pragma once

#include <QObject>
#include <QByteArray>
#include <QUdpSocket>
#include <QHostAddress>
#include <QtGlobal>

class UdpTransport : public QObject
{
    Q_OBJECT

public:
    explicit UdpTransport(QObject* parent = nullptr);

    bool bind(quint16 port);
    bool qosEnabled() const;
    void setQosEnabled(bool enabled);
    void send(const QByteArray& data,
              const QHostAddress& addr,
              quint16 port);
    quint16 localPort() const;

signals:
    void datagramReceived(const QByteArray& data,
                          const QHostAddress& sender,
                          quint16 senderPort);
    void bound(quint16 port);
    void bindFailed(const QString& errorString);

private slots:
    void onReadyRead();

private:
    void applyQosOption();

    QUdpSocket m_socket;
    bool m_qosEnabled = true;
};
