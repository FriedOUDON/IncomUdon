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
#include <algorithm>
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

static QVector<qint16> pcmToSamples(const QByteArray& pcm)
{
    const int sampleCount = pcm.size() / static_cast<int>(sizeof(qint16));
    QVector<qint16> out;
    out.reserve(sampleCount);
    const char* data = pcm.constData();
    for (int i = 0; i < sampleCount; ++i)
    {
        const qint16 sample = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar*>(data + i * static_cast<int>(sizeof(qint16))));
        out.append(sample);
    }
    return out;
}

static QByteArray samplesToPcm(const QVector<qint16>& samples, int offset, int count)
{
    const int boundedOffset = qBound(0, offset, samples.size());
    const int boundedCount = qMax(0, qMin(count, samples.size() - boundedOffset));
    QByteArray out(boundedCount * static_cast<int>(sizeof(qint16)), 0);
    char* ptr = out.data();
    for (int i = 0; i < boundedCount; ++i)
    {
        const qint16 le = qToLittleEndian<qint16>(samples.at(boundedOffset + i));
        std::memcpy(ptr + i * static_cast<int>(sizeof(qint16)), &le, sizeof(le));
    }
    return out;
}

static QByteArray padPcmToSize(const QByteArray& pcm, int targetBytes)
{
    if (targetBytes <= 0)
        return QByteArray();
    if (pcm.size() >= targetBytes)
        return pcm.left(targetBytes);

    QByteArray padded = pcm;
    padded.resize(targetBytes);
    std::memset(padded.data() + pcm.size(), 0, targetBytes - pcm.size());
    return padded;
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

    updatePlayoutParams();
}

ChannelManager::~ChannelManager()
{
    clearStreams();
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
    m_jitterTemplate = jitter;
    updateStreamJitterTargets();
}

void ChannelManager::setCodec(Codec2Wrapper* codec)
{
    if (m_codecTemplate == codec)
        return;

    if (m_codecTemplate)
        disconnect(m_codecTemplate, nullptr, this, nullptr);

    m_codecTemplate = codec;
    if (m_codecTemplate)
    {
        connect(m_codecTemplate, &Codec2Wrapper::frameMsChanged,
                this, &ChannelManager::updatePlayoutParams);
        connect(m_codecTemplate, &Codec2Wrapper::codec2LibraryPathChanged,
                this, [this]() {
                    for (RxStreamState* stream : std::as_const(m_streams))
                        applyTemplateLibraryPaths(stream);
                });
        connect(m_codecTemplate, &Codec2Wrapper::opusLibraryPathChanged,
                this, [this]() {
                    for (RxStreamState* stream : std::as_const(m_streams))
                        applyTemplateLibraryPaths(stream);
                });
    }

    for (RxStreamState* stream : std::as_const(m_streams))
        applyTemplateLibraryPaths(stream);
    updatePlayoutParams();
}

void ChannelManager::setAudioOutput(AudioOutput* output)
{
    m_audioOutput = output;
    if (m_audioOutput)
        m_audioOutput->setSampleRate(m_mixSampleRate);
}

void ChannelManager::setFecEnabled(bool enabled)
{
    if (m_fecEnabled == enabled)
        return;

    m_fecEnabled = enabled;
    for (RxStreamState* stream : std::as_const(m_streams))
    {
        stream->fecDecoder.setEnabled(enabled);
        stream->fecDecoder.reset();
        resetStreamState(stream, true);
    }
    updatePlayoutParams();
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
    m_serverMultiTalkEnabled = false;
    m_serverMaxActiveTalkers = 1;
    m_activeTalkers.clear();
    m_codecConfigCache.clear();
    emitActiveTalkersState();
    clearStreams();

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
    m_serverMultiTalkEnabled = false;
    m_serverMaxActiveTalkers = 1;
    m_joinRetryTimer.stop();
    m_joinRetriesLeft = 0;
    m_activeTalkers.clear();
    m_codecConfigCache.clear();
    emitActiveTalkersState();
    clearStreams();
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

void ChannelManager::clearStreams()
{
    m_playoutTimer.stop();
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
    {
        RxStreamState* stream = it.value();
        delete stream->codec;
        delete stream->jitter;
        delete stream;
    }
    m_streams.clear();
    emitPlayoutTalkersState();
}

QList<quint32> ChannelManager::sortedActiveTalkers() const
{
    QList<quint32> talkers = m_activeTalkers.values();
    std::sort(talkers.begin(), talkers.end());
    return talkers;
}

QList<quint32> ChannelManager::sortedPlayoutTalkers() const
{
    QList<quint32> talkers = m_streams.keys();
    std::sort(talkers.begin(), talkers.end());
    return talkers;
}

void ChannelManager::emitActiveTalkersState()
{
    const QList<quint32> talkers = sortedActiveTalkers();
    emit activeTalkersChanged(talkers);
    emit talkerChanged(talkers.isEmpty() ? 0 : talkers.constFirst());
}

void ChannelManager::emitPlayoutTalkersState()
{
    emit playoutTalkersChanged(sortedPlayoutTalkers());
}

void ChannelManager::applyTemplateLibraryPaths(RxStreamState* stream)
{
    if (!stream || !stream->codec)
        return;

    if (m_codecTemplate)
    {
        stream->codec->setCodec2LibraryPath(m_codecTemplate->codec2LibraryPath());
        stream->codec->setOpusLibraryPath(m_codecTemplate->opusLibraryPath());
    }
}

void ChannelManager::resetStreamState(RxStreamState* stream, bool clearBuffers)
{
    if (!stream)
        return;

    stream->playoutPrimed = false;
    stream->fadeInOnNextFrame = false;
    stream->silenceMode = true;
    stream->talkEnded = false;
    stream->releaseCompletionPending = false;
    stream->pcmMissCount = 0;
    stream->lastPcmFrame.clear();
    stream->pendingMixedSamples.clear();
    stream->resampler.reset();
    stream->fecDecoder.reset();
    if (clearBuffers && stream->jitter)
        stream->jitter->clear();
}

void ChannelManager::applyStreamCodecConfig(RxStreamState* stream, bool resetState)
{
    if (!stream || !stream->codec)
        return;

    const RxCodecConfig config = stream->configKnown ? stream->config
        : RxCodecConfig{m_codecTemplate ? m_codecTemplate->mode() : 1600,
                        (m_codecTemplate && m_codecTemplate->activeCodecTransportId() == Proto::CODEC_TRANSPORT_OPUS)
                            ? Proto::CODEC_TRANSPORT_OPUS
                            : ((m_codecTemplate && m_codecTemplate->forcePcm())
                                   ? Proto::CODEC_TRANSPORT_PCM
                                   : Proto::CODEC_TRANSPORT_CODEC2)};

    if (config.codecId == Proto::CODEC_TRANSPORT_OPUS)
        stream->codec->setCodecType(Codec2Wrapper::CodecTypeOpus);
    else
        stream->codec->setCodecType(Codec2Wrapper::CodecTypeCodec2);
    stream->codec->setForcePcm(config.codecId == Proto::CODEC_TRANSPORT_PCM);
    stream->codec->setMode(config.mode);
    stream->resampler.setRates(qMax(8000, stream->codec->sampleRate()), m_mixSampleRate);

    if (resetState)
        resetStreamState(stream, true);
}

ChannelManager::RxStreamState* ChannelManager::ensureStream(quint32 senderId)
{
    if (senderId == 0)
        return nullptr;

    const auto it = m_streams.constFind(senderId);
    if (it != m_streams.constEnd())
        return it.value();

    RxStreamState* stream = new RxStreamState;
    stream->senderId = senderId;
    stream->codec = new Codec2Wrapper(this);
    stream->jitter = new JitterBuffer(this);
    stream->fecDecoder.setEnabled(m_fecEnabled);
    applyTemplateLibraryPaths(stream);

    const auto configIt = m_codecConfigCache.constFind(senderId);
    if (configIt != m_codecConfigCache.constEnd())
    {
        stream->config = configIt.value();
        stream->configKnown = true;
    }
    applyStreamCodecConfig(stream, false);
    if (stream->jitter)
        stream->jitter->setMinBufferedFrames(streamMinBufferedFrames(stream));
    stream->fadeInOnNextFrame = true;

    m_streams.insert(senderId, stream);
    emitPlayoutTalkersState();
    return stream;
}

void ChannelManager::deleteStream(quint32 senderId)
{
    RxStreamState* stream = m_streams.take(senderId);
    if (!stream)
        return;

    delete stream->codec;
    delete stream->jitter;
    delete stream;
    emitPlayoutTalkersState();
}

int ChannelManager::mixFrameSamples() const
{
    return m_playoutPcmBytes / static_cast<int>(sizeof(qint16));
}

int ChannelManager::streamMinBufferedFrames(const RxStreamState* stream) const
{
    int targetBufferMs = 80;
#ifdef Q_OS_ANDROID
    targetBufferMs = 160;
#elif defined(Q_OS_WINDOWS)
    targetBufferMs = 100;
#endif

    int frames = qMax(2, targetBufferMs / qMax(1, m_playoutFrameMs));
    if (stream && m_fecEnabled)
        frames = qMax(frames, stream->fecDecoder.blockSize() + 2);
    return frames;
}

void ChannelManager::updateStreamJitterTargets()
{
    for (RxStreamState* stream : std::as_const(m_streams))
    {
        if (stream && stream->jitter)
            stream->jitter->setMinBufferedFrames(streamMinBufferedFrames(stream));
    }
}

void ChannelManager::updatePlayoutParams()
{
    int frameMs = 20;
    if (m_codecTemplate && m_codecTemplate->frameMs() > 0)
        frameMs = m_codecTemplate->frameMs();

    const bool changed = frameMs != m_playoutFrameMs;
    m_playoutFrameMs = frameMs;
    m_playoutPcmBytes = qMax(2, (m_mixSampleRate * m_playoutFrameMs * static_cast<int>(sizeof(qint16))) / 1000);
    m_silenceFrame = QByteArray(m_playoutPcmBytes, 0);
    m_playoutTimer.setInterval(m_playoutFrameMs);
    const int samples = mixFrameSamples();
    m_crossfadeSamples = qMax(10, samples / 2);

    if (m_audioOutput)
        m_audioOutput->setSampleRate(m_mixSampleRate);

    updateStreamJitterTargets();

    if (changed)
    {
        for (RxStreamState* stream : std::as_const(m_streams))
            resetStreamState(stream, true);
    }
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
        const quint32 talkerId = readU32Payload(parsed.encryptedPayload, parsed.header.senderId);
        if (type == Proto::PKT_TALK_DENY)
        {
            emit talkDenied(talkerId);
            return;
        }

        if (talkerId == 0)
            return;

        if (type == Proto::PKT_TALK_GRANT)
        {
            m_activeTalkers.insert(talkerId);
            if (RxStreamState* stream = ensureStream(talkerId))
            {
                stream->talkEnded = false;
                stream->releaseCompletionPending = false;
                stream->playoutPrimed = false;
                stream->fadeInOnNextFrame = true;
                stream->silenceMode = true;
                stream->pcmMissCount = 0;
                stream->lastPcmFrame.clear();
                stream->pendingMixedSamples.clear();
                stream->resampler.reset();
                stream->fecDecoder.reset();
                if (stream->jitter)
                    stream->jitter->clear();
            }
            emitActiveTalkersState();
            return;
        }

        emit talkReleasePacketDetected(talkerId);
        m_activeTalkers.remove(talkerId);
        if (RxStreamState* stream = m_streams.value(talkerId, nullptr))
        {
            stream->talkEnded = true;
            stream->releaseCompletionPending = true;
            const bool drained = stream->jitter->size() == 0 &&
                                 stream->pendingMixedSamples.isEmpty() &&
                                 stream->lastPcmFrame.isEmpty();
            if (drained)
            {
                deleteStream(talkerId);
                emit talkReleasePlayoutCompleted(talkerId);
            }
        }
        emitActiveTalkersState();
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
            int codecId = Proto::CODEC_TRANSPORT_CODEC2;
            quint16 mode = 1600;
            if (parsed.encryptedPayload.size() >= 4)
            {
                codecId = static_cast<quint8>(parsed.encryptedPayload.at(1));
                mode = qFromBigEndian<quint16>(
                    reinterpret_cast<const uchar*>(parsed.encryptedPayload.constData() + 2));
            }
            else
            {
                mode = qFromBigEndian<quint16>(
                    reinterpret_cast<const uchar*>(parsed.encryptedPayload.constData() + 1));
            }
            if (pcmOnly)
                codecId = Proto::CODEC_TRANSPORT_PCM;

            RxCodecConfig config{static_cast<int>(mode), codecId};
            m_codecConfigCache.insert(parsed.header.senderId, config);
            if (RxStreamState* stream = m_streams.value(parsed.header.senderId, nullptr))
            {
                const bool changed = !stream->configKnown ||
                    stream->config.mode != config.mode ||
                    stream->config.codecId != config.codecId;
                stream->config = config;
                stream->configKnown = true;
                if (changed)
                    applyStreamCodecConfig(stream, true);
            }
            emit codecConfigReceived(parsed.header.senderId,
                                     static_cast<int>(mode),
                                     pcmOnly,
                                     codecId);
        }
        return;
    }

    if (type == Proto::PKT_SERVER_CONFIG)
    {
        if (parsed.encryptedPayload.size() >= 2)
        {
            const quint16 timeoutSec = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar*>(parsed.encryptedPayload.constData()));
            emit serverTalkTimeoutConfigured(static_cast<int>(timeoutSec));
        }
        if (parsed.encryptedPayload.size() >= 4)
        {
            const quint8 flags = static_cast<quint8>(parsed.encryptedPayload.at(2));
            const int maxActiveTalkers = qMax(1, static_cast<int>(static_cast<quint8>(parsed.encryptedPayload.at(3))));
            m_serverMultiTalkEnabled = (flags & 0x01) != 0;
            m_serverMaxActiveTalkers = maxActiveTalkers;
            emit serverMultiTalkConfigured(m_serverMultiTalkEnabled, m_serverMaxActiveTalkers);
        }
        return;
    }

    if (type != Proto::PKT_AUDIO && type != Proto::PKT_FEC)
        return;

    if (!m_cipher)
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

    RxStreamState* stream = ensureStream(parsed.header.senderId);
    if (!stream)
        return;

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
            stream->fecDecoder.pushParity(blockStart, blockSize, parityIndex, parity);
        for (const FecDecodedFrame& frame : frames)
            stream->jitter->pushFrame(frame.seq, frame.frame);

        if ((!frames.isEmpty() || stream->jitter->size() > 0) && !m_playoutTimer.isActive())
            m_playoutTimer.start();
        return;
    }

    stream->talkEnded = false;
    stream->releaseCompletionPending = false;

    quint16 audioSeq = parsed.header.seq;
    QByteArray frame;
    if (plaintext.size() >= 2)
    {
        audioSeq = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar*>(plaintext.constData()));
        frame = plaintext.mid(2);
    }
    else
    {
        frame = plaintext;
    }
    if (frame.isEmpty())
        return;
    stream->jitter->pushFrame(audioSeq, frame);

    if (m_fecEnabled)
    {
        const QVector<FecDecodedFrame> frames = stream->fecDecoder.pushData(audioSeq, frame);
        for (const FecDecodedFrame& outFrame : frames)
            stream->jitter->pushFrame(outFrame.seq, outFrame.frame);
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

ChannelManager::StreamRenderResult ChannelManager::renderStreamFrame(RxStreamState* stream)
{
    StreamRenderResult result;
    result.pcm = m_silenceFrame;
    if (!stream || !stream->codec || !stream->jitter)
        return result;

    const int targetSamples = mixFrameSamples();
    if (targetSamples <= 0)
        return result;

    if (!stream->playoutPrimed)
    {
        if (stream->jitter->size() < stream->jitter->minBufferedFrames())
        {
            if (stream->talkEnded && stream->jitter->size() == 0 && stream->pendingMixedSamples.isEmpty())
            {
                result.removeStream = true;
                result.releaseCompleted = stream->releaseCompletionPending;
                result.talkerId = stream->senderId;
            }
            return result;
        }
        stream->playoutPrimed = true;
        stream->fadeInOnNextFrame = true;
        stream->silenceMode = false;
    }

    while (stream->pendingMixedSamples.size() < targetSamples)
    {
        const QByteArray encoded = stream->jitter->popFrame(false);
        if (encoded.isEmpty())
            break;

        QByteArray decoded = stream->codec->decode(encoded);
        if (decoded.isEmpty())
            decoded = QByteArray(stream->codec->pcmFrameBytes(), 0);
        const QVector<qint16> input = pcmToSamples(decoded);
        QVector<qint16> resampled;
        stream->resampler.push(input, resampled);
        if (!resampled.isEmpty())
            stream->pendingMixedSamples += resampled;
    }

    if (stream->pendingMixedSamples.size() >= targetSamples)
    {
        QByteArray pcm = samplesToPcm(stream->pendingMixedSamples, 0, targetSamples);
        stream->pendingMixedSamples.remove(0, targetSamples);
        if (stream->fadeInOnNextFrame)
        {
            pcm = crossfadePcm16(m_silenceFrame, pcm, m_crossfadeSamples);
            stream->fadeInOnNextFrame = false;
        }
        stream->lastPcmFrame = pcm;
        stream->silenceMode = false;
        stream->pcmMissCount = 0;
        result.pcm = pcm;
        return result;
    }

    if (stream->talkEnded)
    {
        if (!stream->pendingMixedSamples.isEmpty())
        {
            QByteArray pcm = samplesToPcm(stream->pendingMixedSamples, 0, stream->pendingMixedSamples.size());
            stream->pendingMixedSamples.clear();
            pcm = padPcmToSize(pcm, m_playoutPcmBytes);
            stream->lastPcmFrame = pcm;
            stream->silenceMode = false;
            result.pcm = pcm;
            return result;
        }

        if (!stream->lastPcmFrame.isEmpty() && !stream->silenceMode)
        {
            result.pcm = crossfadePcm16(stream->lastPcmFrame, m_silenceFrame, m_crossfadeSamples);
        }
        result.removeStream = true;
        result.releaseCompleted = stream->releaseCompletionPending;
        result.talkerId = stream->senderId;
        stream->lastPcmFrame.clear();
        stream->silenceMode = true;
        return result;
    }

    ++stream->pcmMissCount;
    if (!stream->lastPcmFrame.isEmpty() && !stream->silenceMode)
    {
        result.pcm = holdDecayFromTailPcm16(stream->lastPcmFrame);
        stream->silenceMode = true;
        return result;
    }

    stream->silenceMode = true;
    return result;
}

void ChannelManager::onPlayoutTick()
{
    if (!m_audioOutput)
        return;

    if (m_streams.isEmpty())
    {
        m_playoutTimer.stop();
        return;
    }

    QVector<int> mix(mixFrameSamples(), 0);
    QList<quint32> removeTalkers;
    QList<quint32> completedTalkers;
    bool anyAudible = false;
    int contributingStreams = 0;

    const QList<quint32> talkers = sortedPlayoutTalkers();
    for (quint32 senderId : talkers)
    {
        RxStreamState* stream = m_streams.value(senderId, nullptr);
        if (!stream)
            continue;

        const StreamRenderResult render = renderStreamFrame(stream);
        const QVector<qint16> samples = pcmToSamples(render.pcm);
        for (int i = 0; i < mix.size() && i < samples.size(); ++i)
            mix[i] += samples.at(i);
        if (render.pcm != m_silenceFrame)
        {
            anyAudible = true;
            ++contributingStreams;
        }
        if (render.removeStream)
        {
            removeTalkers.append(senderId);
            if (render.releaseCompleted && render.talkerId != 0)
                completedTalkers.append(render.talkerId);
        }
    }

    if (mix.isEmpty())
        return;

    QVector<qint16> mixedSamples;
    mixedSamples.reserve(mix.size());
    const int divisor = qMax(1, contributingStreams);
    for (int value : std::as_const(mix))
    {
        const int scaled = value / divisor;
        mixedSamples.append(static_cast<qint16>(qBound(-32768, scaled, 32767)));
    }

    const QByteArray mixedPcm = samplesToPcm(mixedSamples, 0, mixedSamples.size());
    m_audioOutput->playFrame(anyAudible ? mixedPcm : m_silenceFrame);
    emit audioFrameReceived(anyAudible ? mixedPcm : m_silenceFrame);

    for (quint32 senderId : std::as_const(removeTalkers))
        deleteStream(senderId);
    for (quint32 talkerId : std::as_const(completedTalkers))
        emit talkReleasePlayoutCompleted(talkerId);

    if (m_streams.isEmpty())
        m_playoutTimer.stop();
}
