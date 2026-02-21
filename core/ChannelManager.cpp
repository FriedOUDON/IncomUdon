#include "ChannelManager.h"

#include "audio/AudioOutput.h"
#include "codec/Codec2Wrapper.h"
#include "crypto/AeadCipher.h"
#include "net/JitterBuffer.h"
#include "net/udptransport.h"

#include <QAbstractSocket>
#include <QHostInfo>
#include <QtEndian>
#include <QtMath>
#include <cstring>

static quint32 readU32Payload(const QByteArray& payload, quint32 fallback)
{
    if (payload.size() < 4)
        return fallback;

    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(payload.constData()));
}

static QByteArray crossfadePcm16(const QByteArray& fromPcm,
                                 const QByteArray& toPcm,
                                 int fadeSamples)
{
    if (fromPcm.size() != toPcm.size())
        return toPcm;

    const int totalSamples = toPcm.size() / static_cast<int>(sizeof(qint16));
    if (totalSamples <= 0 || fadeSamples <= 0)
        return toPcm;

    const int count = qMin(fadeSamples, totalSamples);
    QByteArray out = toPcm;
    const char* fromPtr = fromPcm.constData();
    const char* toPtr = toPcm.constData();
    char* outPtr = out.data();

    for (int i = 0; i < count; ++i)
    {
        const qint16 a = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(fromPtr + i * static_cast<int>(sizeof(qint16))));
        const qint16 b = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(toPtr + i * static_cast<int>(sizeof(qint16))));
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float v = (1.0f - t) * static_cast<float>(a) + t * static_cast<float>(b);
        const qint16 mixed = static_cast<qint16>(qBound(-32768, static_cast<int>(qRound(v)), 32767));
        const qint16 le = qToLittleEndian<qint16>(mixed);
        std::memcpy(outPtr + i * static_cast<int>(sizeof(qint16)), &le, sizeof(le));
    }

    return out;
}

static QByteArray blendBoundaryPcm16(const QByteArray& prevPcm,
                                     const QByteArray& nextPcm,
                                     int fadeSamples)
{
    if (prevPcm.size() != nextPcm.size())
        return nextPcm;

    const int totalSamples = nextPcm.size() / static_cast<int>(sizeof(qint16));
    if (totalSamples <= 0 || fadeSamples <= 0)
        return nextPcm;

    const int count = qMin(fadeSamples, totalSamples);
    QByteArray out = nextPcm;
    const char* prevPtr = prevPcm.constData();
    const char* nextPtr = nextPcm.constData();
    char* outPtr = out.data();
    const int prevStart = totalSamples - count;

    for (int i = 0; i < count; ++i)
    {
        const qint16 a = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(prevPtr + (prevStart + i) * static_cast<int>(sizeof(qint16))));
        const qint16 b = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(nextPtr + i * static_cast<int>(sizeof(qint16))));
        const float t = static_cast<float>(i + 1) / static_cast<float>(count + 1);
        const float v = (1.0f - t) * static_cast<float>(a) + t * static_cast<float>(b);
        const qint16 mixed = static_cast<qint16>(qBound(-32768, static_cast<int>(qRound(v)), 32767));
        const qint16 le = qToLittleEndian<qint16>(mixed);
        std::memcpy(outPtr + i * static_cast<int>(sizeof(qint16)), &le, sizeof(le));
    }

    return out;
}

static QByteArray scalePcm16(const QByteArray& pcm, float gain)
{
    if (pcm.isEmpty() || gain >= 0.999f)
        return pcm;

    QByteArray out = pcm;
    const char* inPtr = pcm.constData();
    char* outPtr = out.data();
    const int totalSamples = pcm.size() / static_cast<int>(sizeof(qint16));
    for (int i = 0; i < totalSamples; ++i)
    {
        const qint16 sample = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(inPtr + i * static_cast<int>(sizeof(qint16))));
        const float scaled = static_cast<float>(sample) * gain;
        const qint16 mixed = static_cast<qint16>(qBound(-32768, static_cast<int>(qRound(scaled)), 32767));
        const qint16 le = qToLittleEndian<qint16>(mixed);
        std::memcpy(outPtr + i * static_cast<int>(sizeof(qint16)), &le, sizeof(le));
    }
    return out;
}

static QByteArray holdDecayFromTailPcm16(const QByteArray& pcm)
{
    if (pcm.isEmpty())
        return pcm;

    const int totalSamples = pcm.size() / static_cast<int>(sizeof(qint16));
    if (totalSamples <= 0)
        return pcm;

    const qint16 tail = qFromLittleEndian<qint16>(
        reinterpret_cast<const uchar*>(pcm.constData() +
        (totalSamples - 1) * static_cast<int>(sizeof(qint16))));

    QByteArray out(pcm.size(), 0);
    char* outPtr = out.data();
    const int denom = qMax(1, totalSamples - 1);
    for (int i = 0; i < totalSamples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(denom);
        const float v = static_cast<float>(tail) * (1.0f - t);
        const qint16 sample = static_cast<qint16>(qBound(-32768, static_cast<int>(qRound(v)), 32767));
        const qint16 le = qToLittleEndian<qint16>(sample);
        std::memcpy(outPtr + i * static_cast<int>(sizeof(qint16)), &le, sizeof(le));
    }
    return out;
}

ChannelManager::ChannelManager(QObject* parent)
    : QObject(parent)
{
    m_joinRetryTimer.setInterval(m_joinRetryMs);
    m_joinRetryTimer.setSingleShot(false);
    connect(&m_joinRetryTimer, &QTimer::timeout,
            this, &ChannelManager::onJoinRetryTimeout);

    m_playoutTimer.setInterval(m_playoutFrameMs);
    m_playoutTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playoutTimer, &QTimer::timeout,
            this, &ChannelManager::onPlayoutTick);
}

void ChannelManager::setTransport(UdpTransport* transport)
{
    if (m_transport == transport)
        return;

    if (m_transport)
        disconnect(m_transport, nullptr, this, nullptr);

    m_transport = transport;
    if (m_transport)
    {
        connect(m_transport, &UdpTransport::datagramReceived,
                this, &ChannelManager::onDatagramReceived);
    }
}

void ChannelManager::setPacketizer(Packetizer* packetizer)
{
    m_packetizer = packetizer;
}

void ChannelManager::setCipher(AeadCipher* cipher)
{
    m_cipher = cipher;
}

void ChannelManager::setJitterBuffer(JitterBuffer* jitter)
{
    m_jitter = jitter;
}

void ChannelManager::setCodec(Codec2Wrapper* codec)
{
    m_codec = codec;
    if (m_codec)
    {
        connect(m_codec, &Codec2Wrapper::frameMsChanged,
                this, &ChannelManager::updatePlayoutParams);
        connect(m_codec, &Codec2Wrapper::pcmFrameBytesChanged,
                this, &ChannelManager::updatePlayoutParams);
        connect(m_codec, &Codec2Wrapper::forcePcmChanged,
                this, &ChannelManager::updatePlayoutParams);
    }
    updatePlayoutParams();
}

void ChannelManager::setAudioOutput(AudioOutput* output)
{
    m_audioOutput = output;
}

void ChannelManager::setFecEnabled(bool enabled)
{
    if (m_fecEnabled == enabled)
        return;

    m_fecEnabled = enabled;
    m_fecDecoder.setEnabled(enabled);
    m_fecDecoder.reset();
    m_playoutPrimed = false;
    m_plcRemaining = 0;
    m_pcmMissCount = 0;
    m_talkEnded = false;
    m_releaseTalkerId = 0;
    m_silenceMode = false;
    m_lastPcmFrame.clear();
    m_fadeFromPcm.clear();
    m_fadeOutPending = false;
    m_fadeOutFrame.clear();
    m_fadeInOnNextFrame = true;
    updatePlayoutParams();
    if (m_jitter)
        m_jitter->clear();
}

bool ChannelManager::connectToServer(int channelId,
                                     const QString& address,
                                     int port,
                                     const QString& password)
{
    ChannelConfig config;
    if (channelId <= 0)
    {
        emit channelError(QStringLiteral("Invalid channel id"));
        return false;
    }

    const QString addressText = address.trimmed();
    if (addressText.isEmpty())
    {
        emit channelError(QStringLiteral("Invalid server address"));
        return false;
    }

    QHostAddress host;
    if (!host.setAddress(addressText))
    {
        if (addressText.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        {
            host = QHostAddress::LocalHost;
        }
        else
        {
            const QHostInfo info = QHostInfo::fromName(addressText);
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty())
            {
                emit channelError(QStringLiteral("Failed to resolve server address"));
                return false;
            }

            // Prefer IPv4 because current UDP bind mode is AnyIPv4.
            for (const QHostAddress& candidate : info.addresses())
            {
                if (candidate.protocol() == QAbstractSocket::IPv4Protocol)
                {
                    host = candidate;
                    break;
                }
            }
            if (host.isNull())
                host = info.addresses().constFirst();
        }
    }

    if (port <= 0 || port > 65535)
    {
        emit channelError(QStringLiteral("Invalid server port"));
        return false;
    }

    config.channelId = static_cast<quint32>(channelId);
    config.address = host;
    config.port = static_cast<quint16>(port);
    config.password = password;

    const bool ok = joinChannel(config);
    if (ok)
    {
        emit channelConfigured(config.channelId,
                               config.address.toString(),
                               config.port,
                               config.password);
    }
    return ok;
}

void ChannelManager::disconnectFromServer()
{
    leaveChannel();
}

bool ChannelManager::joinChannel(const ChannelConfig& config)
{
    if (!m_transport || !m_packetizer)
    {
        emit channelError(QStringLiteral("Transport or Packetizer is not set"));
        return false;
    }

    m_config = config;
    m_packetizer->setChannelId(config.channelId);
    m_packetizer->setUseLegacy(false);
    m_serverLocked = false;
    m_joinRetriesLeft = 5;
    m_playoutPrimed = false;
    m_fadeFromPcm.clear();
    m_fadeOutPending = false;
    m_fadeOutFrame.clear();
    m_silenceMode = false;
    m_plcRemaining = 0;
    m_pcmMissCount = 0;
    m_talkEnded = false;
    m_releaseTalkerId = 0;
    m_fecDecoder.reset();
    if (m_jitter)
        m_jitter->clear();

    emit channelIdChanged();
    emit targetChanged();
    emit channelReady();

    const QByteArray joinPacket = m_packetizer->packPlain(Proto::PKT_JOIN, QByteArray());
    m_transport->send(joinPacket, m_config.address, m_config.port);
    if (m_packetizer && !m_packetizer->useLegacy())
    {
        const QByteArray legacyJoin = m_packetizer->packPlainLegacy(Proto::PKT_JOIN, QByteArray());
        m_transport->send(legacyJoin, m_config.address, m_config.port);
    }
    if (!m_joinRetryTimer.isActive())
        m_joinRetryTimer.start();
    return true;
}

void ChannelManager::leaveChannel()
{
    if (m_transport && m_packetizer && m_config.port != 0)
    {
        const QByteArray leavePacket = m_packetizer->packPlain(Proto::PKT_LEAVE, QByteArray());
        m_transport->send(leavePacket, m_config.address, m_config.port);
    }

    m_config = {};
    m_serverLocked = false;
    m_joinRetryTimer.stop();
    m_joinRetriesLeft = 0;
    m_playoutPrimed = false;
    m_fadeFromPcm.clear();
    m_fadeOutPending = false;
    m_fadeOutFrame.clear();
    m_silenceMode = false;
    m_plcRemaining = 0;
    m_pcmMissCount = 0;
    m_talkEnded = false;
    m_releaseTalkerId = 0;
    m_fecDecoder.reset();
    if (m_jitter)
        m_jitter->clear();
    m_playoutTimer.stop();
    emit channelIdChanged();
    emit targetChanged();
}

quint32 ChannelManager::channelId() const
{
    return m_config.channelId;
}

QString ChannelManager::targetAddress() const
{
    return m_config.address.toString();
}

quint16 ChannelManager::targetPort() const
{
    return m_config.port;
}

void ChannelManager::onDatagramReceived(const QByteArray& datagram,
                                        const QHostAddress& sender,
                                        quint16 senderPort)
{
    if (!m_packetizer)
        return;

    ParsedPacket parsed;
    if (!m_packetizer->unpack(datagram, parsed))
        return;

    if (parsed.header.channelId != m_config.channelId)
        return;

    if (m_packetizer && !m_packetizer->useLegacy() &&
        (parsed.header.headerLen == Proto::LEGACY_FIXED_HEADER_SIZE ||
         parsed.header.headerLen == Proto::LEGACY_FIXED_HEADER_SIZE + Proto::SECURITY_HEADER_SIZE))
    {
        m_packetizer->setUseLegacy(true);
    }

    if (m_serverLocked)
    {
        if (!m_config.address.isNull() && sender != m_config.address)
            return;
    }
    else
    {
        if (!m_config.address.isNull() && sender != m_config.address)
        {
            m_config.address = sender;
            emit targetChanged();
        }

        if (senderPort != 0 && senderPort != m_config.port)
        {
            m_config.port = senderPort;
            emit targetChanged();
        }

        m_serverLocked = true;
    }

    m_joinRetryTimer.stop();
    m_joinRetriesLeft = 0;
    emit serverActivity();

    const quint8 type = parsed.header.type;
    if (type == Proto::PKT_TALK_GRANT ||
        type == Proto::PKT_TALK_RELEASE ||
        type == Proto::PKT_TALK_DENY)
    {
        quint32 talkerId = readU32Payload(parsed.encryptedPayload, parsed.header.senderId);
        if (type == Proto::PKT_TALK_RELEASE)
        {
            const quint32 releasedTalkerId = talkerId;
            m_releaseTalkerId = releasedTalkerId;
            emit talkReleasePacketDetected(releasedTalkerId);
            talkerId = 0;
        }
        if (talkerId != 0)
        {
            // New talker; re-prime playout to avoid repeating stale frames.
            m_playoutPrimed = false;
            m_lastPcmFrame.clear();
            m_fadeFromPcm.clear();
            m_fadeOutPending = false;
            m_fadeOutFrame.clear();
            m_silenceMode = false;
            m_plcRemaining = 0;
            m_pcmMissCount = 0;
            m_talkEnded = false;
            m_releaseTalkerId = 0;
            m_fecDecoder.reset();
            if (m_jitter)
                m_jitter->clear();
        }
        else
        {
            // Talk ended: drain already-buffered frames, then stop gracefully.
            m_talkEnded = true;
        }
        emit talkerChanged(talkerId);
        if (type == Proto::PKT_TALK_DENY)
        {
            emit talkDenied(talkerId);
        }
        return;
    }

    if (type == Proto::PKT_KEY_EXCHANGE)
    {
        emit handshakeReceived(parsed.encryptedPayload);
        return;
    }

    if (type == Proto::PKT_CODEC_CONFIG)
    {
        if (parsed.encryptedPayload.size() >= 3)
        {
            const quint8 flags = static_cast<quint8>(parsed.encryptedPayload.at(0));
            const bool pcmOnly = (flags & 0x01) != 0;
            const quint16 mode = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar*>(parsed.encryptedPayload.constData() + 1));
            emit codecConfigReceived(parsed.header.senderId,
                                     static_cast<int>(mode),
                                     pcmOnly);
        }
        return;
    }

    if (type != Proto::PKT_AUDIO && type != Proto::PKT_FEC)
        return;

    if (!m_cipher || !m_codec || !m_jitter)
        return;

    QByteArray plaintext;
    if (!m_cipher->decrypt(parsed.encryptedPayload,
                           parsed.authTag,
                           parsed.sec.nonce,
                           QByteArray(),
                           plaintext))
    {
        return;
    }

    if (type == Proto::PKT_FEC)
    {
        if (!m_fecEnabled || plaintext.size() < 4)
            return;

        const quint16 blockStart = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar*>(plaintext.constData()));
        const quint8 blockSize = static_cast<quint8>(plaintext.at(2));
        const quint8 parityIndex = static_cast<quint8>(plaintext.at(3));
        const QByteArray parity = plaintext.mid(4);

        const QVector<FecDecodedFrame> frames =
            m_fecDecoder.pushParity(blockStart, blockSize, parityIndex, parity);
        for (const FecDecodedFrame& frame : frames)
            m_jitter->pushFrame(frame.seq, frame.frame);

        if (!frames.isEmpty() && !m_playoutTimer.isActive())
            m_playoutTimer.start();
        return;
    }

    m_talkEnded = false;
    m_releaseTalkerId = 0;

    // Parse audio payload in one unified path for both FEC ON/OFF.
    // New format: [audioSeq:2][codecFrame...]
    // Legacy format: [codecFrame...] (no audioSeq in payload)
    const int expectedFrame = m_codec ? m_codec->frameBytes() : 0;
    quint16 audioSeq = parsed.header.seq;
    QByteArray frame = plaintext;
    if (expectedFrame > 0 && plaintext.size() == expectedFrame)
    {
        audioSeq = parsed.header.seq;
        frame = plaintext;
    }
    else if (plaintext.size() >= 2)
    {
        audioSeq = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar*>(plaintext.constData()));
        frame = plaintext.mid(2);
    }
    m_jitter->pushFrame(audioSeq, frame);

    if (m_fecEnabled)
    {
        const QVector<FecDecodedFrame> frames = m_fecDecoder.pushData(audioSeq, frame);
        for (const FecDecodedFrame& outFrame : frames)
            m_jitter->pushFrame(outFrame.seq, outFrame.frame);
    }

    if (!m_playoutTimer.isActive())
        m_playoutTimer.start();
}

void ChannelManager::onJoinRetryTimeout()
{
    if (m_serverLocked)
    {
        m_joinRetryTimer.stop();
        return;
    }

    if (m_joinRetriesLeft <= 0)
    {
        m_joinRetryTimer.stop();
        return;
    }

    if (!m_transport || !m_packetizer)
        return;

    const QByteArray joinPacket = m_packetizer->packPlain(Proto::PKT_JOIN, QByteArray());
    m_transport->send(joinPacket, m_config.address, m_config.port);
    if (m_packetizer && !m_packetizer->useLegacy())
    {
        const QByteArray legacyJoin = m_packetizer->packPlainLegacy(Proto::PKT_JOIN, QByteArray());
        m_transport->send(legacyJoin, m_config.address, m_config.port);
    }
    m_joinRetriesLeft--;
}

void ChannelManager::updatePlayoutParams()
{
    int frameMs = 20;
    int pcmBytes = 320;
    if (m_codec)
    {
        if (m_codec->frameMs() > 0)
            frameMs = m_codec->frameMs();
        if (m_codec->pcmFrameBytes() > 0)
            pcmBytes = m_codec->pcmFrameBytes();
    }

    const bool changed = (frameMs != m_playoutFrameMs) ||
                         (pcmBytes != m_playoutPcmBytes);
    m_playoutFrameMs = frameMs;
    m_playoutPcmBytes = pcmBytes;
    m_silenceFrame = QByteArray(m_playoutPcmBytes, 0);
    m_playoutTimer.setInterval(m_playoutFrameMs);
    const int samples = m_playoutPcmBytes / static_cast<int>(sizeof(qint16));
    m_crossfadeSamples = qMax(10, samples / 2);

    int targetBufferMs = 80;
#ifdef Q_OS_ANDROID
    targetBufferMs = 160;
#endif
    if (m_codec && m_codec->forcePcm() && !m_fecEnabled)
    {
#ifdef Q_OS_ANDROID
        targetBufferMs = 260;
#else
        targetBufferMs = 200;
#endif
    }
    if (m_jitter && m_playoutFrameMs > 0)
    {
        int frames = targetBufferMs / m_playoutFrameMs;
        if (frames < 2)
            frames = 2;
        if (m_fecEnabled)
            frames = qMax(frames, m_fecDecoder.blockSize() + 2);
        m_jitter->setMinBufferedFrames(frames);
    }

    if (changed)
    {
        m_playoutPrimed = false;
        if (!m_lastPcmFrame.isEmpty())
        {
            m_fadeOutPending = true;
            m_fadeOutFrame = m_lastPcmFrame;
        }
        else
        {
            m_fadeOutPending = false;
            m_fadeOutFrame.clear();
        }
        m_lastPcmFrame.clear();
        m_fadeFromPcm.clear();
        m_fadeInOnNextFrame = true;
        m_silenceMode = false;
        m_plcRemaining = 0;
        m_pcmMissCount = 0;
        m_talkEnded = false;
        m_releaseTalkerId = 0;
        m_fecDecoder.reset();
        if (m_jitter)
            m_jitter->clear();
    }
}

void ChannelManager::onPlayoutTick()
{
    if (!m_audioOutput || !m_codec || !m_jitter)
        return;

    if (!m_playoutPrimed)
    {
        if (m_jitter->size() < m_jitter->minBufferedFrames())
            return;
        m_playoutPrimed = true;
    }

    if (m_fadeOutPending)
    {
        const QByteArray faded = crossfadePcm16(m_fadeOutFrame, m_silenceFrame, m_crossfadeSamples);
        m_audioOutput->playFrame(faded);
        emit audioFrameReceived(faded);
        m_fadeOutPending = false;
        m_fadeOutFrame.clear();
        m_silenceMode = true;
        m_plcRemaining = 0;
        m_pcmMissCount = 0;
        return;
    }

    const int targetFrames = m_jitter->minBufferedFrames();
    const int size = m_jitter->size();
    const bool pcmMode = (m_codec && m_codec->forcePcm());

    if (m_talkEnded && size == 0)
    {
        const quint32 releasedTalkerId = m_releaseTalkerId;
        if (!m_silenceMode && !m_lastPcmFrame.isEmpty())
        {
            const QByteArray faded = crossfadePcm16(m_lastPcmFrame, m_silenceFrame, m_crossfadeSamples);
            m_audioOutput->playFrame(faded);
            emit audioFrameReceived(faded);
        }
        else
        {
            m_audioOutput->playFrame(m_silenceFrame);
            emit audioFrameReceived(m_silenceFrame);
        }
        m_silenceMode = true;
        m_plcRemaining = 0;
        m_pcmMissCount = 0;
        m_lastPcmFrame.clear();
        m_fadeFromPcm.clear();
        m_talkEnded = false;
        m_releaseTalkerId = 0;
        if (releasedTalkerId != 0)
            emit talkReleasePlayoutCompleted(releasedTalkerId);
        return;
    }

    int dropMargin = 2;
    if (m_fecEnabled || pcmMode)
        dropMargin = m_fecDecoder.blockSize() / 2 + 2;
    const bool allowDropCorrection = true;
    if (allowDropCorrection && size > targetFrames + dropMargin)
    {
        // Drop extra frames to keep latency bounded without changing pitch.
        QByteArray lastDropped;
        while (m_jitter->size() > targetFrames + dropMargin)
        {
            const QByteArray dropped = m_jitter->popFrame(false);
            if (dropped.isEmpty())
                break;
            lastDropped = dropped;
        }
        if (!lastDropped.isEmpty())
        {
            if (!m_lastPcmFrame.isEmpty())
            {
                m_fadeFromPcm = m_lastPcmFrame;
            }
            else
            {
                const QByteArray droppedPcm = m_codec->decode(lastDropped);
                if (!droppedPcm.isEmpty())
                    m_fadeFromPcm = droppedPcm;
            }
        }
    }

    QByteArray encoded = m_jitter->popFrame(false);
    if (encoded.isEmpty())
    {
        if (pcmMode)
        {
            if (m_audioOutput && m_audioOutput->queuedMs() > (m_playoutFrameMs * 2))
            {
                // Output side still has buffered audio; do not inject synthetic frames yet.
                return;
            }
            ++m_pcmMissCount;
            if (m_pcmMissCount <= 1 && !m_lastPcmFrame.isEmpty())
            {
                const QByteArray plc = holdDecayFromTailPcm16(m_lastPcmFrame);
                m_audioOutput->playFrame(plc);
                emit audioFrameReceived(plc);
                return;
            }
            if (!m_silenceMode && !m_lastPcmFrame.isEmpty())
            {
                const QByteArray faded = crossfadePcm16(m_lastPcmFrame,
                                                        m_silenceFrame,
                                                        m_crossfadeSamples);
                m_audioOutput->playFrame(faded);
                emit audioFrameReceived(faded);
                m_silenceMode = true;
                m_plcRemaining = 0;
                return;
            }
            m_silenceMode = true;
            m_plcRemaining = 0;
            m_audioOutput->playFrame(m_silenceFrame);
            emit audioFrameReceived(m_silenceFrame);
            return;
        }

        if (!m_silenceMode)
            m_silenceMode = true;

        const int plcFrames = m_plcMaxFrames;
        if (!m_lastPcmFrame.isEmpty())
        {
            if (m_plcRemaining == 0)
                m_plcRemaining = plcFrames;

            if (m_plcRemaining > 0)
            {
                float gain = static_cast<float>(m_plcRemaining) /
                             static_cast<float>(qMax(1, plcFrames));
                const QByteArray plc = scalePcm16(m_lastPcmFrame, gain);
                m_audioOutput->playFrame(plc);
                emit audioFrameReceived(plc);
                m_plcRemaining--;
                if (m_plcRemaining == 0)
                    m_plcRemaining = -1;
                return;
            }
        }
        m_plcRemaining = 0;

        m_audioOutput->playFrame(m_silenceFrame);
        emit audioFrameReceived(m_silenceFrame);
        return;
    }

    m_pcmMissCount = 0;
    if (m_silenceMode)
    {
        m_fadeInOnNextFrame = true;
        m_silenceMode = false;
    }
    m_plcRemaining = 0;

    QByteArray pcm = m_codec->decode(encoded);
    if (pcm.isEmpty())
        pcm = m_silenceFrame;

    if (!m_fadeFromPcm.isEmpty())
    {
        pcm = blendBoundaryPcm16(m_fadeFromPcm, pcm, m_crossfadeSamples);
        m_fadeFromPcm.clear();
    }
    if (m_fadeInOnNextFrame)
    {
        pcm = crossfadePcm16(m_silenceFrame, pcm, m_crossfadeSamples);
        m_fadeInOnNextFrame = false;
    }

    m_audioOutput->playFrame(pcm);
    emit audioFrameReceived(pcm);
    m_lastPcmFrame = pcm;
}
