#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QtGlobal>

#include "audio/AudioResampler.h"
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
    ~ChannelManager() override;

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
    void activeTalkersChanged(const QList<quint32>& talkerIds);
    void playoutTalkersChanged(const QList<quint32>& talkerIds);
    void talkReleasePacketDetected(quint32 talkerId);
    void talkReleasePlayoutCompleted(quint32 talkerId);
    void talkDenied(quint32 currentTalkerId);
    void handshakeReceived(const QByteArray& payload);
    void codecConfigReceived(quint32 senderId, int codecMode, bool pcmOnly, int codecId);
    void serverTalkTimeoutConfigured(int timeoutSec);
    void serverMultiTalkConfigured(bool enabled, int maxActiveTalkers);
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
    struct RxCodecConfig
    {
        int mode = 1600;
        int codecId = Proto::CODEC_TRANSPORT_CODEC2;
    };

    struct RxStreamState
    {
        quint32 senderId = 0;
        Codec2Wrapper* codec = nullptr;
        JitterBuffer* jitter = nullptr;
        FecDecoder fecDecoder;
        AudioResampler resampler;
        RxCodecConfig config;
        bool configKnown = false;
        bool playoutPrimed = false;
        bool fadeInOnNextFrame = false;
        bool silenceMode = true;
        bool talkEnded = false;
        bool releaseCompletionPending = false;
        int pcmMissCount = 0;
        QByteArray lastPcmFrame;
        QVector<qint16> pendingMixedSamples;
    };

    struct StreamRenderResult
    {
        QByteArray pcm;
        bool removeStream = false;
        bool releaseCompleted = false;
        quint32 talkerId = 0;
    };

    void clearStreams();
    QList<quint32> sortedActiveTalkers() const;
    QList<quint32> sortedPlayoutTalkers() const;
    void emitActiveTalkersState();
    void emitPlayoutTalkersState();
    RxStreamState* ensureStream(quint32 senderId);
    void deleteStream(quint32 senderId);
    void applyTemplateLibraryPaths(RxStreamState* stream);
    void applyStreamCodecConfig(RxStreamState* stream, bool resetState);
    void resetStreamState(RxStreamState* stream, bool clearBuffers);
    int mixFrameSamples() const;
    int streamMinBufferedFrames(const RxStreamState* stream) const;
    void updateStreamJitterTargets();
    void updatePlayoutParams();
    StreamRenderResult renderStreamFrame(RxStreamState* stream);

    ChannelConfig m_config;
    UdpTransport* m_transport = nullptr;
    Packetizer* m_packetizer = nullptr;
    AeadCipher* m_cipher = nullptr;
    JitterBuffer* m_jitterTemplate = nullptr;
    Codec2Wrapper* m_codecTemplate = nullptr;
    AudioOutput* m_audioOutput = nullptr;
    QTimer m_playoutTimer;
    int m_playoutFrameMs = 20;
    int m_playoutPcmBytes = 640;
    int m_crossfadeSamples = 40;
    int m_mixSampleRate = 16000;
    QByteArray m_silenceFrame;
    QHash<quint32, RxStreamState*> m_streams;
    QHash<quint32, RxCodecConfig> m_codecConfigCache;
    QSet<quint32> m_activeTalkers;
    bool m_fecEnabled = false;
    bool m_serverMultiTalkEnabled = false;
    int m_serverMaxActiveTalkers = 1;
    bool m_serverLocked = false;
    QTimer m_joinRetryTimer;
    int m_joinRetryMs = 1000;
    int m_joinRetriesLeft = 0;
};
