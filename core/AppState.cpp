#include "AppState.h"

#include <QtGlobal>
#include <QRandomGenerator>

AppState::AppState(QObject* parent)
    : QObject(parent)
{
}

int AppState::cryptoMode() const
{
    return m_cryptoMode;
}

void AppState::setCryptoMode(int mode)
{
    int normalized = (mode == CryptoLegacyXor) ? CryptoLegacyXor : CryptoAesGcm;

#ifndef INCOMUDON_USE_OPENSSL
    normalized = CryptoLegacyXor;
#endif

    if (m_cryptoMode == normalized)
        return;

    m_cryptoMode = normalized;
    emit cryptoModeChanged();
}

bool AppState::opensslAvailable() const
{
#ifdef INCOMUDON_USE_OPENSSL
    return true;
#else
    return false;
#endif
}

bool AppState::opusAvailable() const
{
#ifdef INCOMUDON_USE_OPUS
    return true;
#else
    return false;
#endif
}

int AppState::currentChannelId() const
{
    return m_currentChannelId;
}

void AppState::setCurrentChannelId(int channelId)
{
    if (m_currentChannelId == channelId)
        return;

    m_currentChannelId = channelId;
    emit currentChannelIdChanged();
}

bool AppState::pttPressed() const
{
    return m_pttPressed;
}

void AppState::setPttPressed(bool pressed)
{
    if (m_pttPressed == pressed)
        return;

    m_pttPressed = pressed;
    emit pttPressedChanged();
}

QString AppState::linkStatus() const
{
    return m_linkStatus;
}

void AppState::setLinkStatus(const QString& status)
{
    if (m_linkStatus == status)
        return;

    m_linkStatus = status;
    emit linkStatusChanged();
}

float AppState::rxLevel() const
{
    return m_rxLevel;
}

void AppState::setRxLevel(float level)
{
    if (qFuzzyCompare(m_rxLevel, level))
        return;

    m_rxLevel = level;
    emit rxLevelChanged();
}

float AppState::txLevel() const
{
    return m_txLevel;
}

void AppState::setTxLevel(float level)
{
    if (qFuzzyCompare(m_txLevel, level))
        return;

    m_txLevel = level;
    emit txLevelChanged();
}

quint32 AppState::talkerId() const
{
    return m_talkerId;
}

void AppState::setTalkerId(quint32 talkerId)
{
    if (m_talkerId == talkerId)
        return;

    m_talkerId = talkerId;
    emit talkerIdChanged();
}

bool AppState::serverOnline() const
{
    return m_serverOnline;
}

void AppState::setServerOnline(bool online)
{
    if (m_serverOnline == online)
        return;

    m_serverOnline = online;
    emit serverOnlineChanged();
}

int AppState::talkTimeoutSec() const
{
    return m_talkTimeoutSec;
}

void AppState::setTalkTimeoutSec(int sec)
{
    const int normalized = qBound(0, sec, 86400);
    if (m_talkTimeoutSec == normalized)
        return;

    m_talkTimeoutSec = normalized;
    emit talkTimeoutSecChanged();
}

quint32 AppState::selfId() const
{
    return m_selfId;
}

void AppState::setSelfId(quint32 selfId)
{
    if (m_selfId == selfId)
        return;

    m_selfId = selfId;
    emit selfIdChanged();
}

quint32 AppState::senderId() const
{
    return m_senderId;
}

void AppState::setSenderId(quint32 senderId)
{
    static constexpr quint32 kMinSenderId = 1u;
    static constexpr quint32 kMaxSenderId = 0x7FFFFFFFu;

    quint32 normalized = senderId;
    if (senderId < kMinSenderId || senderId > kMaxSenderId)
    {
        normalized = QRandomGenerator::global()->bounded(kMinSenderId, kMaxSenderId + 1u);
    }
    if (m_senderId == normalized)
        return;

    m_senderId = normalized;
    emit senderIdChanged();
}

int AppState::codecSelection() const
{
    return m_codecSelection;
}

static int normalizeCodecBitrateForSelection(int bitrate, int selection);
static int selectRememberedBitrateForSelection(int codec2Bitrate, int opusBitrate, int selection);

void AppState::setCodecSelection(int selection)
{
    int normalized = CodecPcm;
    if (selection == CodecCodec2 || selection == CodecOpus)
        normalized = selection;

#ifndef INCOMUDON_USE_OPUS
    if (normalized == CodecOpus)
        normalized = CodecCodec2;
#endif

    const bool newForcePcm = (normalized == CodecPcm);
    const bool selectionChanged = (m_codecSelection != normalized);
    const bool forceChanged = (m_forcePcm != newForcePcm);
    const int normalizedBitrate = selectRememberedBitrateForSelection(
        m_codec2Bitrate, m_opusBitrate, normalized);
    const bool bitrateChanged = (m_codecBitrate != normalizedBitrate);
    if (!selectionChanged && !forceChanged && !bitrateChanged)
        return;

    m_codecSelection = normalized;
    m_forcePcm = newForcePcm;
    m_codecBitrate = normalizedBitrate;
    if (normalized == CodecOpus)
        m_opusBitrate = normalizedBitrate;
    else
        m_codec2Bitrate = normalizedBitrate;
    if (selectionChanged)
        emit codecSelectionChanged();
    if (forceChanged)
        emit forcePcmChanged();
    if (bitrateChanged)
        emit codecBitrateChanged();
}

int AppState::codecBitrate() const
{
    return m_codecBitrate;
}

static int nearestOption(int value, const int* options, int count)
{
    int best = options[0];
    int bestDiff = qAbs(value - options[0]);
    for (int i = 1; i < count; ++i)
    {
        const int diff = qAbs(value - options[i]);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = options[i];
        }
    }
    return best;
}

static int normalizeCodec2Bitrate(int bitrate)
{
    static constexpr int options[] = {450, 700, 1600, 2400, 3200};
    return nearestOption(bitrate, options, 5);
}

static int normalizeOpusBitrate(int bitrate)
{
    if (bitrate < 6000)
        return 6000;

    static constexpr int options[] = {6000, 8000, 12000, 16000, 20000, 64000, 96000, 128000};
    return nearestOption(bitrate, options, 8);
}

static int normalizeCodecBitrateForSelection(int bitrate, int selection)
{
    if (selection == AppState::CodecOpus)
        return normalizeOpusBitrate(bitrate);
    return normalizeCodec2Bitrate(bitrate);
}

static int selectRememberedBitrateForSelection(int codec2Bitrate, int opusBitrate, int selection)
{
    if (selection == AppState::CodecOpus)
        return normalizeCodecBitrateForSelection(opusBitrate, selection);
    return normalizeCodecBitrateForSelection(codec2Bitrate, selection);
}

void AppState::setCodecBitrate(int bitrate)
{
    const int normalized = normalizeCodecBitrateForSelection(bitrate, m_codecSelection);
    if (m_codecSelection == CodecOpus)
        m_opusBitrate = normalized;
    else
        m_codec2Bitrate = normalized;

    if (m_codecBitrate == normalized)
        return;

    m_codecBitrate = normalized;
    emit codecBitrateChanged();
}

bool AppState::forcePcm() const
{
    return m_forcePcm;
}

void AppState::setForcePcm(bool force)
{
    const bool normalized = force;
    int newSelection = m_codecSelection;
    if (normalized)
    {
        newSelection = CodecPcm;
    }
    else if (newSelection == CodecPcm)
    {
        newSelection = CodecCodec2;
    }

#ifndef INCOMUDON_USE_OPUS
    if (newSelection == CodecOpus)
        newSelection = CodecCodec2;
#endif

    const int normalizedBitrate = selectRememberedBitrateForSelection(
        m_codec2Bitrate, m_opusBitrate, newSelection);
    const bool bitrateChanged = (m_codecBitrate != normalizedBitrate);
    if (m_forcePcm == normalized &&
        m_codecSelection == newSelection &&
        !bitrateChanged)
        return;

    const bool selectionChanged = (m_codecSelection != newSelection);
    m_forcePcm = normalized;
    m_codecSelection = newSelection;
    m_codecBitrate = normalizedBitrate;
    if (newSelection == CodecOpus)
        m_opusBitrate = normalizedBitrate;
    else
        m_codec2Bitrate = normalizedBitrate;
    emit forcePcmChanged();
    if (selectionChanged)
        emit codecSelectionChanged();
    if (bitrateChanged)
        emit codecBitrateChanged();
}

bool AppState::fecEnabled() const
{
    return m_fecEnabled;
}

void AppState::setFecEnabled(bool enabled)
{
    if (m_fecEnabled == enabled)
        return;

    m_fecEnabled = enabled;
    emit fecEnabledChanged();
}

bool AppState::qosEnabled() const
{
    return m_qosEnabled;
}

void AppState::setQosEnabled(bool enabled)
{
    if (m_qosEnabled == enabled)
        return;

    m_qosEnabled = enabled;
    emit qosEnabledChanged();
}

int AppState::micVolumePercent() const
{
    return m_micVolumePercent;
}

void AppState::setMicVolumePercent(int percent)
{
    const int normalized = qBound(0, percent, 300);
    if (m_micVolumePercent == normalized)
        return;

    m_micVolumePercent = normalized;
    emit micVolumePercentChanged();
}

bool AppState::noiseSuppressionEnabled() const
{
    return m_noiseSuppressionEnabled;
}

void AppState::setNoiseSuppressionEnabled(bool enabled)
{
    if (m_noiseSuppressionEnabled == enabled)
        return;

    m_noiseSuppressionEnabled = enabled;
    emit noiseSuppressionEnabledChanged();
}

int AppState::noiseSuppressionLevel() const
{
    return m_noiseSuppressionLevel;
}

void AppState::setNoiseSuppressionLevel(int level)
{
    const int normalized = qBound(0, level, 100);
    if (m_noiseSuppressionLevel == normalized)
        return;

    m_noiseSuppressionLevel = normalized;
    emit noiseSuppressionLevelChanged();
}

int AppState::speakerVolumePercent() const
{
    return m_speakerVolumePercent;
}

void AppState::setSpeakerVolumePercent(int percent)
{
    const int normalized = qBound(0, percent, 400);
    if (m_speakerVolumePercent == normalized)
        return;

    m_speakerVolumePercent = normalized;
    emit speakerVolumePercentChanged();
}

bool AppState::keepMicSessionAlwaysOn() const
{
    return m_keepMicSessionAlwaysOn;
}

void AppState::setKeepMicSessionAlwaysOn(bool enabled)
{
    if (m_keepMicSessionAlwaysOn == enabled)
        return;

    m_keepMicSessionAlwaysOn = enabled;
    emit keepMicSessionAlwaysOnChanged();
}

QString AppState::codec2LibraryPath() const
{
    return m_codec2LibraryPath;
}

void AppState::setCodec2LibraryPath(const QString& path)
{
    if (m_codec2LibraryPath == path)
        return;

    m_codec2LibraryPath = path;
    emit codec2LibraryPathChanged();
}

bool AppState::codec2LibraryLoaded() const
{
    return m_codec2LibraryLoaded;
}

void AppState::setCodec2LibraryLoaded(bool loaded)
{
    if (m_codec2LibraryLoaded == loaded)
        return;

    m_codec2LibraryLoaded = loaded;
    emit codec2LibraryLoadedChanged();
}

QString AppState::codec2LibraryError() const
{
    return m_codec2LibraryError;
}

void AppState::setCodec2LibraryError(const QString& error)
{
    if (m_codec2LibraryError == error)
        return;

    m_codec2LibraryError = error;
    emit codec2LibraryErrorChanged();
}

QString AppState::opusLibraryPath() const
{
    return m_opusLibraryPath;
}

void AppState::setOpusLibraryPath(const QString& path)
{
    if (m_opusLibraryPath == path)
        return;

    m_opusLibraryPath = path;
    emit opusLibraryPathChanged();
}

bool AppState::opusLibraryLoaded() const
{
    return m_opusLibraryLoaded;
}

void AppState::setOpusLibraryLoaded(bool loaded)
{
    if (m_opusLibraryLoaded == loaded)
        return;

    m_opusLibraryLoaded = loaded;
    emit opusLibraryLoadedChanged();
}

QString AppState::opusLibraryError() const
{
    return m_opusLibraryError;
}

void AppState::setOpusLibraryError(const QString& error)
{
    if (m_opusLibraryError == error)
        return;

    m_opusLibraryError = error;
    emit opusLibraryErrorChanged();
}
