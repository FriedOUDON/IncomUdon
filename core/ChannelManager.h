#pragma once

#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include "net/Packetizer.h"
#include "net/Fec.h"

class AeadCipher;
class Codec2Wrapper;
class JitterBuffer;
class UdpTransport;
class AudioOutput;

struct ChannelConfig
{
    quint32 channelId = 0;
    QHostAddress address;
    quint16 port = 0;
    QString password;
};

class ChannelManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(quint32 channelId
               READ channelId
               NOTIFY channelIdChanged)
    Q_PROPERTY(QString targetAddress
               READ targetAddress
               NOTIFY targetChanged)
    Q_PROPERTY(quint16 targetPort
               READ targetPort
               NOTIFY targetChanged)

public:
    explicit ChannelManager(QObject* parent = nullptr);

    void setTransport(UdpTransport* transport);
    void setPacketizer(Packetizer* packetizer);
    void setCipher(AeadCipher* cipher);
    void setJitterBuffer(JitterBuffer* jitter);
    void setCodec(Codec2Wrapper* codec);
    void setAudioOutput(AudioOutput* output);
    void setFecEnabled(bool enabled);

    Q_INVOKABLE bool connectToServer(int channelId,
                                     const QString& address,
                                     int port,
                                     const QString& password);
    Q_INVOKABLE void disconnectFromServer();

    bool joinChannel(const ChannelConfig& config);
    void leaveChannel();

    quint32 channelId() const;
    QString targetAddress() const;
    quint16 targetPort() const;

signals:
    void channelReady();
    void channelError(const QString& message);
    void channelIdChanged();
    void targetChanged();
    void audioFrameReceived(const QByteArray& pcmFrame);
    void talkerChanged(quint32 talkerId);
    void talkReleasePacketDetected(quint32 talkerId);
    void talkReleasePlayoutCompleted(quint32 talkerId);
    void talkDenied(quint32 currentTalkerId);
    void handshakeReceived(const QByteArray& payload);
    void codecConfigReceived(quint32 senderId, int codecMode, bool pcmOnly);
    void channelConfigured(quint32 channelId,
                           const QString& address,
                           quint16 port,
                           const QString& password);
    void serverActivity();

private slots:
    void onDatagramReceived(const QByteArray& datagram,
                            const QHostAddress& sender,
                            quint16 senderPort);
    void onJoinRetryTimeout();
    void onPlayoutTick();

private:
    void updatePlayoutParams();

    ChannelConfig m_config;
    UdpTransport* m_transport = nullptr;
    Packetizer* m_packetizer = nullptr;
    AeadCipher* m_cipher = nullptr;
    JitterBuffer* m_jitter = nullptr;
    Codec2Wrapper* m_codec = nullptr;
    AudioOutput* m_audioOutput = nullptr;
    QTimer m_playoutTimer;
    bool m_playoutPrimed = false;
    int m_playoutFrameMs = 20;
    int m_playoutPcmBytes = 320;
    int m_crossfadeSamples = 40;
    QByteArray m_silenceFrame;
    QByteArray m_lastPcmFrame;
    QByteArray m_fadeFromPcm;
    bool m_fadeInOnNextFrame = false;
    bool m_fadeOutPending = false;
    QByteArray m_fadeOutFrame;
    int m_plcRemaining = 0;
    int m_plcMaxFrames = 3;
    int m_pcmMissCount = 0;
    bool m_talkEnded = false;
    quint32 m_releaseTalkerId = 0;
    bool m_silenceMode = false;
    bool m_fecEnabled = false;
    FecDecoder m_fecDecoder;
    bool m_serverLocked = false;
    QTimer m_joinRetryTimer;
    int m_joinRetryMs = 1000;
    int m_joinRetriesLeft = 0;
};
