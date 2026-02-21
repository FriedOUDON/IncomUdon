#include "AppState.h"

#include <QtGlobal>

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

int AppState::codecBitrate() const
{
    return m_codecBitrate;
}

static int normalizeCodecBitrate(int bitrate)
{
    const int options[] = {450, 700, 1600, 2400, 3200};
    int best = options[0];
    int bestDiff = qAbs(bitrate - options[0]);
    for (int i = 1; i < 5; ++i)
    {
        const int diff = qAbs(bitrate - options[i]);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = options[i];
        }
    }
    return best;
}

void AppState::setCodecBitrate(int bitrate)
{
    const int normalized = normalizeCodecBitrate(bitrate);
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
    if (m_forcePcm == force)
        return;

    m_forcePcm = force;
    emit forcePcmChanged();
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

int AppState::micVolumePercent() const
{
    return m_micVolumePercent;
}

void AppState::setMicVolumePercent(int percent)
{
    const int normalized = qBound(0, percent, 200);
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
