#pragma once

#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>
#include <QtGlobal>

#include "crypto/AeadCipher.h"
#include "net/Fec.h"

class AudioInput;
class Codec2Wrapper;
class Packetizer;
class UdpTransport;

class PttController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool pttPressed
               READ pttPressed
               WRITE setPttPressed
               NOTIFY pttPressedChanged)

public:
    explicit PttController(QObject* parent = nullptr);

    void setAudioInput(AudioInput* input);
    void setCodec(Codec2Wrapper* codec);
    void setCipher(AeadCipher* cipher);
    void setPacketizer(Packetizer* packetizer);
    void setTransport(UdpTransport* transport);
    void setFecEnabled(bool enabled);
    void setAlwaysKeepInputSession(bool enabled);
    void setRxHoldActive(bool active);

    void setTarget(const QHostAddress& address, quint16 port);

    bool pttPressed() const;

public slots:
    void setPttPressed(bool pressed);
    void setTalkAllowed(bool allowed);

signals:
    void pttPressedChanged();
    void txStarted();
    void txStopped();

private slots:
    void onAudioFrameReady(const QByteArray& pcmFrame);
    void onInputRunningChanged();
    void onTxTick();
    void onDelayedTxStart();
    void onInputIdleTimeout();

private:
    void tryStartTx();
    void sendCodecFrame(const QByteArray& codecFrame);
    void ensureInputSession();
    void scheduleInputIdleStop();

    AudioInput* m_audioInput = nullptr;
    Codec2Wrapper* m_codec = nullptr;
    AeadCipher* m_cipher = nullptr;
    Packetizer* m_packetizer = nullptr;
    UdpTransport* m_transport = nullptr;

    QHostAddress m_targetAddress;
    quint16 m_targetPort = 0;
    bool m_pttPressed = false;
    bool m_talkAllowed = false;
    bool m_pendingPttOff = false;
    bool m_fecEnabled = false;
    bool m_alwaysKeepInputSession = false;
    bool m_rxHoldActive = false;
    quint16 m_audioSeq = 0;
    FecEncoder m_fec;
    QTimer m_txTimer;
    QTimer m_txStartDelayTimer;
    QTimer m_inputIdleTimer;
    QElapsedTimer m_pttPressedElapsed;
    int m_txStartGuardMs = 0;
    int m_inputIdleTimeoutMs = 60000;
    QQueue<QByteArray> m_txQueue;
    int m_txQueueMaxFrames = 12;
};
