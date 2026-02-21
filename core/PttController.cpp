#include "PttController.h"

#include "audio/AudioInput.h"
#include "codec/Codec2Wrapper.h"
#include "net/Packetizer.h"
#include "net/udptransport.h"

#include <QtEndian>
#include <cstring>

PttController::PttController(QObject* parent)
    : QObject(parent)
{
    m_txTimer.setTimerType(Qt::PreciseTimer);
    m_txTimer.setInterval(20);
    connect(&m_txTimer, &QTimer::timeout,
            this, &PttController::onTxTick);

    m_txStartDelayTimer.setSingleShot(true);
    connect(&m_txStartDelayTimer, &QTimer::timeout,
            this, &PttController::onDelayedTxStart);

    m_inputIdleTimer.setSingleShot(true);
    m_inputIdleTimer.setInterval(m_inputIdleTimeoutMs);
    connect(&m_inputIdleTimer, &QTimer::timeout,
            this, &PttController::onInputIdleTimeout);

#ifdef Q_OS_ANDROID
    // Keep a short guard to avoid unstable start on some Android devices.
    m_txStartGuardMs = 0;
#endif
}

void PttController::setAudioInput(AudioInput* input)
{
    if (m_audioInput == input)
        return;

    if (m_audioInput)
        disconnect(m_audioInput, nullptr, this, nullptr);

    m_audioInput = input;
    if (m_audioInput)
    {
        connect(m_audioInput, &AudioInput::frameReady,
                this, &PttController::onAudioFrameReady);
        connect(m_audioInput, &AudioInput::runningChanged,
                this, &PttController::onInputRunningChanged);
    }
}

void PttController::setCodec(Codec2Wrapper* codec)
{
    m_codec = codec;
}

void PttController::setCipher(AeadCipher* cipher)
{
    m_cipher = cipher;
}

void PttController::setPacketizer(Packetizer* packetizer)
{
    m_packetizer = packetizer;
}

void PttController::setTransport(UdpTransport* transport)
{
    m_transport = transport;
}

void PttController::setFecEnabled(bool enabled)
{
    if (m_fecEnabled == enabled)
        return;

    m_fecEnabled = enabled;
    m_fec.setEnabled(enabled);
    m_fec.reset();
    m_audioSeq = 0;
}

void PttController::setAlwaysKeepInputSession(bool enabled)
{
    if (m_alwaysKeepInputSession == enabled)
        return;

    m_alwaysKeepInputSession = enabled;
    if (m_alwaysKeepInputSession)
    {
        m_inputIdleTimer.stop();
        ensureInputSession();
    }
    else
    {
        scheduleInputIdleStop();
    }
}

void PttController::setRxHoldActive(bool active)
{
    if (m_rxHoldActive == active)
        return;

    m_rxHoldActive = active;
    if (m_rxHoldActive)
    {
        m_inputIdleTimer.stop();
        ensureInputSession();
    }
    else
    {
        scheduleInputIdleStop();
    }
}

void PttController::setTarget(const QHostAddress& address, quint16 port)
{
    m_targetAddress = address;
    m_targetPort = port;
}

bool PttController::pttPressed() const
{
    return m_pttPressed;
}

void PttController::setPttPressed(bool pressed)
{
    if (m_pttPressed == pressed)
        return;

    m_pttPressed = pressed;
    emit pttPressedChanged();

    if (!pressed)
    {
        m_txStartDelayTimer.stop();

        // Keep draining queued TX audio first; send PTT_OFF afterwards.
        m_pendingPttOff = true;
        if (!m_txTimer.isActive())
            m_txTimer.start();
        if (m_txQueue.isEmpty())
            onTxTick();
        scheduleInputIdleStop();
        return;
    }

    m_inputIdleTimer.stop();
    ensureInputSession();
    if (!m_audioInput || !m_audioInput->isRunning())
        return;

    m_pendingPttOff = false;
    if (m_packetizer && m_transport &&
        !m_targetAddress.isNull() && m_targetPort != 0)
    {
        const QByteArray packet = m_packetizer->packPlain(Proto::PKT_PTT_ON, QByteArray());
        m_transport->send(packet, m_targetAddress, m_targetPort);
    }

    m_pttPressedElapsed.restart();
    tryStartTx();
}

void PttController::setTalkAllowed(bool allowed)
{
    if (m_talkAllowed == allowed)
        return;

    m_talkAllowed = allowed;

    if (!m_audioInput)
        return;

    if (m_pttPressed && m_talkAllowed)
    {
        m_pendingPttOff = false;
        tryStartTx();
    }
    else
    {
        m_txStartDelayTimer.stop();

        // If release is pending, keep timer alive until queued frames are handled.
        if (!m_pttPressed && m_pendingPttOff)
        {
            if (!m_txTimer.isActive())
                m_txTimer.start();
            if (m_txQueue.isEmpty())
                onTxTick();
            return;
        }

        m_pendingPttOff = false;
        m_txTimer.stop();
        m_txQueue.clear();
        m_fec.reset();
        m_audioSeq = 0;
        emit txStopped();
        scheduleInputIdleStop();
    }
}

void PttController::onDelayedTxStart()
{
    tryStartTx();
}

void PttController::tryStartTx()
{
    if (!m_audioInput || !m_pttPressed || !m_talkAllowed)
        return;

    m_inputIdleTimer.stop();

    if (m_txStartGuardMs > 0 && m_pttPressedElapsed.isValid())
    {
        const qint64 elapsed = m_pttPressedElapsed.elapsed();
        if (elapsed < m_txStartGuardMs)
        {
            const int remain = qMax(1, m_txStartGuardMs - static_cast<int>(elapsed));
            m_txStartDelayTimer.start(remain);
            return;
        }
    }

    m_txStartDelayTimer.stop();
    ensureInputSession();
    if (!m_audioInput->isRunning())
        return;

    const int frameMs = (m_codec && m_codec->frameMs() > 0) ? m_codec->frameMs() : 20;
    m_txTimer.setInterval(frameMs);
    m_txQueue.clear();
    m_fec.reset();
    m_audioSeq = 0;
    m_txTimer.start();
    emit txStarted();
}

void PttController::onAudioFrameReady(const QByteArray& pcmFrame)
{
    if (!m_pttPressed || !m_talkAllowed)
        return;

    if (!m_codec || !m_cipher || !m_packetizer || !m_transport)
        return;

    if (!m_cipher->isReady())
        return;

    const QByteArray codecFrame = m_codec->encode(pcmFrame);
    if (m_codec && m_codec->frameMs() > 0 && m_txTimer.interval() != m_codec->frameMs())
        m_txTimer.setInterval(m_codec->frameMs());

    m_txQueue.enqueue(codecFrame);
    while (m_txQueue.size() > m_txQueueMaxFrames)
        m_txQueue.dequeue();

    if (!m_txTimer.isActive())
        m_txTimer.start();
}

void PttController::onInputRunningChanged()
{
    if (!m_audioInput)
        return;

    if (!m_audioInput->isRunning())
        return;

    if (m_pttPressed && m_talkAllowed)
    {
        m_pendingPttOff = false;
        tryStartTx();
    }
}

void PttController::onTxTick()
{
    const bool canSendAudio = m_codec && m_cipher && m_packetizer && m_transport &&
                              m_cipher->isReady() && m_talkAllowed;

    if (canSendAudio && !m_txQueue.isEmpty())
    {
        sendCodecFrame(m_txQueue.dequeue());
        return;
    }

    if (m_pendingPttOff)
    {
        // If we can't send remaining queued frames anymore, drop them and finish.
        if (!m_txQueue.isEmpty())
            m_txQueue.clear();

        if (m_packetizer && m_transport &&
            !m_targetAddress.isNull() && m_targetPort != 0)
        {
            const QByteArray packet = m_packetizer->packPlain(Proto::PKT_PTT_OFF, QByteArray());
            m_transport->send(packet, m_targetAddress, m_targetPort);
        }

        m_pendingPttOff = false;
        m_txTimer.stop();
        m_fec.reset();
        m_audioSeq = 0;
        emit txStopped();
        scheduleInputIdleStop();
        return;
    }

    if (!m_pttPressed && m_txQueue.isEmpty())
    {
        m_txTimer.stop();
        scheduleInputIdleStop();
    }
}

void PttController::onInputIdleTimeout()
{
    if (!m_audioInput)
        return;
    if (m_alwaysKeepInputSession || m_rxHoldActive ||
        m_pttPressed || m_pendingPttOff || m_txTimer.isActive())
        return;

    m_audioInput->stop();
}

void PttController::ensureInputSession()
{
    if (!m_audioInput)
        return;
    if (m_audioInput->isRunning())
        return;

    m_audioInput->start();
}

void PttController::scheduleInputIdleStop()
{
    if (!m_audioInput)
        return;
    if (m_alwaysKeepInputSession)
    {
        m_inputIdleTimer.stop();
        ensureInputSession();
        return;
    }
    if (m_rxHoldActive || m_pttPressed || m_pendingPttOff || m_txTimer.isActive())
    {
        m_inputIdleTimer.stop();
        return;
    }
    if (!m_audioInput->isRunning())
        return;
    m_inputIdleTimer.start();
}

void PttController::sendCodecFrame(const QByteArray& codecFrame)
{
    const quint64 nonce = m_cipher->nextNonce();

    QByteArray payload(2 + codecFrame.size(), 0);
    qToBigEndian(m_audioSeq, reinterpret_cast<uchar*>(payload.data()));
    if (!codecFrame.isEmpty())
        std::memcpy(payload.data() + 2, codecFrame.constData(), codecFrame.size());

    const AeadResult enc = m_cipher->encrypt(payload, nonce, QByteArray());
    const QByteArray packet = m_packetizer->pack(Proto::PKT_AUDIO,
                                                 enc.ciphertext,
                                                 enc.tag,
                                                 nonce);

    if (!m_targetAddress.isNull() && m_targetPort != 0)
        m_transport->send(packet, m_targetAddress, m_targetPort);

    if (m_fecEnabled)
    {
        const QVector<FecParityPacket> parity = m_fec.addFrame(m_audioSeq, codecFrame);
        for (const FecParityPacket& pkt : parity)
        {
            QByteArray fecPayload(4, 0);
            qToBigEndian(pkt.blockStart, reinterpret_cast<uchar*>(fecPayload.data()));
            fecPayload[2] = static_cast<char>(pkt.blockSize);
            fecPayload[3] = static_cast<char>(pkt.parityIndex);
            fecPayload.append(pkt.data);

            const quint64 fecNonce = m_cipher->nextNonce();
            const AeadResult fecEnc = m_cipher->encrypt(fecPayload, fecNonce, QByteArray());
            const QByteArray fecPacket = m_packetizer->pack(Proto::PKT_FEC,
                                                            fecEnc.ciphertext,
                                                            fecEnc.tag,
                                                            fecNonce);
            if (!m_targetAddress.isNull() && m_targetPort != 0)
                m_transport->send(fecPacket, m_targetAddress, m_targetPort);
        }
    }
    m_audioSeq++;
}
