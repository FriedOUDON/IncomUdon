#pragma once

#include <QObject>
#include <QString>

class AppState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int cryptoMode
               READ cryptoMode
               WRITE setCryptoMode
               NOTIFY cryptoModeChanged)
    Q_PROPERTY(bool opensslAvailable
               READ opensslAvailable
               CONSTANT)
    Q_PROPERTY(bool opusAvailable
               READ opusAvailable
               CONSTANT)
    Q_PROPERTY(int currentChannelId
               READ currentChannelId
               WRITE setCurrentChannelId
               NOTIFY currentChannelIdChanged)
    Q_PROPERTY(bool pttPressed
               READ pttPressed
               WRITE setPttPressed
               NOTIFY pttPressedChanged)
    Q_PROPERTY(QString linkStatus
               READ linkStatus
               WRITE setLinkStatus
               NOTIFY linkStatusChanged)
    Q_PROPERTY(float rxLevel
               READ rxLevel
               WRITE setRxLevel
               NOTIFY rxLevelChanged)
    Q_PROPERTY(float txLevel
               READ txLevel
               WRITE setTxLevel
               NOTIFY txLevelChanged)
    Q_PROPERTY(quint32 talkerId
               READ talkerId
               WRITE setTalkerId
               NOTIFY talkerIdChanged)
    Q_PROPERTY(bool serverOnline
               READ serverOnline
               WRITE setServerOnline
               NOTIFY serverOnlineChanged)
    Q_PROPERTY(quint32 selfId
               READ selfId
               WRITE setSelfId
               NOTIFY selfIdChanged)
    Q_PROPERTY(quint32 senderId
               READ senderId
               WRITE setSenderId
               NOTIFY senderIdChanged)
    Q_PROPERTY(int codecSelection
               READ codecSelection
               WRITE setCodecSelection
               NOTIFY codecSelectionChanged)
    Q_PROPERTY(int codecBitrate
               READ codecBitrate
               WRITE setCodecBitrate
               NOTIFY codecBitrateChanged)
    Q_PROPERTY(bool forcePcm
               READ forcePcm
               WRITE setForcePcm
               NOTIFY forcePcmChanged)
    Q_PROPERTY(bool fecEnabled
               READ fecEnabled
               WRITE setFecEnabled
               NOTIFY fecEnabledChanged)
    Q_PROPERTY(int micVolumePercent
               READ micVolumePercent
               WRITE setMicVolumePercent
               NOTIFY micVolumePercentChanged)
    Q_PROPERTY(bool noiseSuppressionEnabled
               READ noiseSuppressionEnabled
               WRITE setNoiseSuppressionEnabled
               NOTIFY noiseSuppressionEnabledChanged)
    Q_PROPERTY(int noiseSuppressionLevel
               READ noiseSuppressionLevel
               WRITE setNoiseSuppressionLevel
               NOTIFY noiseSuppressionLevelChanged)
    Q_PROPERTY(int speakerVolumePercent
               READ speakerVolumePercent
               WRITE setSpeakerVolumePercent
               NOTIFY speakerVolumePercentChanged)
    Q_PROPERTY(bool keepMicSessionAlwaysOn
               READ keepMicSessionAlwaysOn
               WRITE setKeepMicSessionAlwaysOn
               NOTIFY keepMicSessionAlwaysOnChanged)
    Q_PROPERTY(QString codec2LibraryPath
               READ codec2LibraryPath
               WRITE setCodec2LibraryPath
               NOTIFY codec2LibraryPathChanged)
    Q_PROPERTY(bool codec2LibraryLoaded
               READ codec2LibraryLoaded
               WRITE setCodec2LibraryLoaded
               NOTIFY codec2LibraryLoadedChanged)
    Q_PROPERTY(QString codec2LibraryError
               READ codec2LibraryError
               WRITE setCodec2LibraryError
               NOTIFY codec2LibraryErrorChanged)
    Q_PROPERTY(QString opusLibraryPath
               READ opusLibraryPath
               WRITE setOpusLibraryPath
               NOTIFY opusLibraryPathChanged)
    Q_PROPERTY(bool opusLibraryLoaded
               READ opusLibraryLoaded
               WRITE setOpusLibraryLoaded
               NOTIFY opusLibraryLoadedChanged)
    Q_PROPERTY(QString opusLibraryError
               READ opusLibraryError
               WRITE setOpusLibraryError
               NOTIFY opusLibraryErrorChanged)

public:
    enum CryptoMode {
        CryptoAesGcm = 0,
        CryptoLegacyXor = 1
    };
    Q_ENUM(CryptoMode)

    enum CodecSelection {
        CodecPcm = 0,
        CodecCodec2 = 1,
        CodecOpus = 2
    };
    Q_ENUM(CodecSelection)

    explicit AppState(QObject* parent = nullptr);

    int cryptoMode() const;
    void setCryptoMode(int mode);
    bool opensslAvailable() const;
    bool opusAvailable() const;

    int currentChannelId() const;
    void setCurrentChannelId(int channelId);

    bool pttPressed() const;
    void setPttPressed(bool pressed);

    QString linkStatus() const;
    void setLinkStatus(const QString& status);

    float rxLevel() const;
    void setRxLevel(float level);

    float txLevel() const;
    void setTxLevel(float level);

    quint32 talkerId() const;
    void setTalkerId(quint32 talkerId);

    bool serverOnline() const;
    void setServerOnline(bool online);

    quint32 selfId() const;
    void setSelfId(quint32 selfId);
    quint32 senderId() const;
    void setSenderId(quint32 senderId);
    int codecSelection() const;
    void setCodecSelection(int selection);

    int codecBitrate() const;
    void setCodecBitrate(int bitrate);
    bool forcePcm() const;
    void setForcePcm(bool force);
    bool fecEnabled() const;
    void setFecEnabled(bool enabled);
    int micVolumePercent() const;
    void setMicVolumePercent(int percent);
    bool noiseSuppressionEnabled() const;
    void setNoiseSuppressionEnabled(bool enabled);
    int noiseSuppressionLevel() const;
    void setNoiseSuppressionLevel(int level);
    int speakerVolumePercent() const;
    void setSpeakerVolumePercent(int percent);
    bool keepMicSessionAlwaysOn() const;
    void setKeepMicSessionAlwaysOn(bool enabled);
    QString codec2LibraryPath() const;
    void setCodec2LibraryPath(const QString& path);
    bool codec2LibraryLoaded() const;
    void setCodec2LibraryLoaded(bool loaded);
    QString codec2LibraryError() const;
    void setCodec2LibraryError(const QString& error);
    QString opusLibraryPath() const;
    void setOpusLibraryPath(const QString& path);
    bool opusLibraryLoaded() const;
    void setOpusLibraryLoaded(bool loaded);
    QString opusLibraryError() const;
    void setOpusLibraryError(const QString& error);

signals:
    void cryptoModeChanged();
    void currentChannelIdChanged();
    void pttPressedChanged();
    void linkStatusChanged();
    void rxLevelChanged();
    void txLevelChanged();
    void talkerIdChanged();
    void serverOnlineChanged();
    void selfIdChanged();
    void senderIdChanged();
    void codecSelectionChanged();
    void codecBitrateChanged();
    void forcePcmChanged();
    void fecEnabledChanged();
    void micVolumePercentChanged();
    void noiseSuppressionEnabledChanged();
    void noiseSuppressionLevelChanged();
    void speakerVolumePercentChanged();
    void keepMicSessionAlwaysOnChanged();
    void codec2LibraryPathChanged();
    void codec2LibraryLoadedChanged();
    void codec2LibraryErrorChanged();
    void opusLibraryPathChanged();
    void opusLibraryLoadedChanged();
    void opusLibraryErrorChanged();

private:
    int m_cryptoMode =
#ifdef INCOMUDON_USE_OPENSSL
        CryptoAesGcm;
#else
        CryptoLegacyXor;
#endif
    int m_currentChannelId = 0;
    bool m_pttPressed = false;
    QString m_linkStatus = QStringLiteral("Disconnected");
    float m_rxLevel = 0.0f;
    float m_txLevel = 0.0f;
    quint32 m_talkerId = 0;
    bool m_serverOnline = false;
    quint32 m_selfId = 0;
    quint32 m_senderId = 0;
    int m_codecSelection = CodecPcm;
    int m_codecBitrate = 1600;
    bool m_forcePcm = true;
    bool m_fecEnabled = true;
    int m_micVolumePercent = 100;
    bool m_noiseSuppressionEnabled = false;
    int m_noiseSuppressionLevel = 45;
    int m_speakerVolumePercent = 100;
    bool m_keepMicSessionAlwaysOn = false;
    QString m_codec2LibraryPath;
    bool m_codec2LibraryLoaded = false;
    QString m_codec2LibraryError;
    QString m_opusLibraryPath;
    bool m_opusLibraryLoaded = false;
    QString m_opusLibraryError;
};
