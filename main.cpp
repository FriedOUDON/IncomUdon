#include <QCoreApplication>
#include <QGuiApplication>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRandomGenerator>
#include <QTimer>
#include <QtEndian>
#include <QDebug>
#include <QStringList>
#include <functional>
#ifdef Q_OS_ANDROID
#include <android/log.h>
#include <QJniObject>
#endif

#include "audio/AudioInput.h"
#include "audio/AudioOutput.h"
#include "codec/Codec2Wrapper.h"
#include "core/AndroidPttBridge.h"
#include "core/AppState.h"
#include "core/ChannelManager.h"
#include "core/PttController.h"
#include "crypto/AeadCipher.h"
#include "crypto/KeyExchange.h"
#include "net/JitterBuffer.h"
#include "net/Packetizer.h"
#include "net/udptransport.h"

namespace {
static void logCodecStatus(const QString& message)
{
    qWarning().noquote() << message;
#ifdef Q_OS_ANDROID
    const QByteArray utf8 = message.toUtf8();
    __android_log_print(ANDROID_LOG_WARN, "IncomUdon", "%s", utf8.constData());
#endif
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    AppState appState;
    AudioInput audioInput;
    AudioOutput audioOutput;
    Codec2Wrapper codecTx;
    // Use a single codec2 wrapper instance for both TX/RX to avoid
    // creating a second runtime codec2 context on Android.
    Codec2Wrapper& codecRx = codecTx;
    KeyExchange keyExchange;
    AeadCipher cipher;
    Packetizer packetizer;
    JitterBuffer jitter;
    UdpTransport transport;
    ChannelManager channelManager;
    PttController pttController;
    QHostAddress currentServerAddress;
    quint16 currentServerPort = 0;
    bool suppressCodecBroadcast = false;
    int lastSentCodecMode = -1;
    int lastSentCodecId = -1;
    quint32 lastRxConfigSender = 0;
    int lastRxConfigMode = -1;
    int lastRxConfigCodecId = -1;
    QHostAddress lastSentAddress;
    quint16 lastSentPort = 0;
    std::function<void(bool)> sendCodecConfig = [](bool) {};
    QElapsedTimer lastHandshakeTimer;
    QByteArray lastHandshakePayload;
    bool txActive = false;
    std::function<void()> updateAndroidBackgroundReceiveService = []() {};

    QObject::connect(&keyExchange, &KeyExchange::sessionKeyReady,
                     &cipher, [&cipher](const QByteArray& key,
                                        const QByteArray& nonceBase,
                                        KeyExchange::CryptoMode mode) {
        cipher.setKey(key, nonceBase);
        const bool legacy = (mode == KeyExchange::CryptoMode::LegacyXor);
        cipher.setMode(legacy ? AeadCipher::Mode::LegacyXor
                              : AeadCipher::Mode::AesGcm);
    });

    QObject::connect(&keyExchange, &KeyExchange::handshakePacketReady,
                     &transport, [&packetizer, &transport, &currentServerAddress, &currentServerPort,
                                  &lastHandshakeTimer, &lastHandshakePayload](const QByteArray& payload) {
        if (currentServerPort == 0 || currentServerAddress.isNull())
            return;
        if (payload == lastHandshakePayload)
        {
            if (!lastHandshakeTimer.isValid())
                lastHandshakeTimer.start();
            if (lastHandshakeTimer.elapsed() < 1000)
                return;
        }
        const QByteArray packet = packetizer.packPlain(Proto::PKT_KEY_EXCHANGE, payload);
        transport.send(packet, currentServerAddress, currentServerPort);
        lastHandshakePayload = payload;
        if (lastHandshakeTimer.isValid())
            lastHandshakeTimer.restart();
        else
            lastHandshakeTimer.start();
    });

    QObject::connect(&pttController, &PttController::txStarted,
                     &appState, [&appState, &txActive]() {
        txActive = true;
        appState.setLinkStatus(QStringLiteral("TX"));
    });
    QObject::connect(&pttController, &PttController::txStopped,
                     &appState, [&appState, &txActive]() {
        txActive = false;
        if (appState.serverOnline())
            appState.setLinkStatus(QStringLiteral("Ready"));
        else
            appState.setLinkStatus(QStringLiteral("No response"));
    });

    QObject::connect(&appState, &AppState::pttPressedChanged,
                     &pttController, [&appState, &pttController]() {
        pttController.setPttPressed(appState.pttPressed());
    });

    AndroidPttBridge& androidPttBridge = AndroidPttBridge::instance();

#ifdef Q_OS_ANDROID
    androidPttBridge.initialize();
    auto isLikelyBluetoothInputName = [](const QString& name) {
        const QString n = name.toLower();
        return n.contains(QStringLiteral("bluetooth")) ||
               n.contains(QStringLiteral("ble")) ||
               n.contains(QStringLiteral("headset")) ||
               n.contains(QStringLiteral("hands-free")) ||
               n.contains(QStringLiteral("wireless"));
    };
    auto updateAndroidCommunicationModePreference =
        [&audioInput, &androidPttBridge, isLikelyBluetoothInputName]() {
        const QString selectedId = audioInput.selectedInputDeviceId();
        const QStringList ids = audioInput.inputDeviceIds();
        const QStringList names = audioInput.inputDeviceNames();

        int selectedIndex = 0;
        if (!selectedId.isEmpty())
        {
            const int found = ids.indexOf(selectedId);
            if (found >= 0)
                selectedIndex = found;
        }

        QString selectedName;
        if (selectedIndex >= 0 && selectedIndex < names.size())
            selectedName = names.at(selectedIndex);

        const bool preferCommunicationMode = isLikelyBluetoothInputName(selectedName);
        androidPttBridge.setPreferCommunicationMode(preferCommunicationMode);
    };
    QObject::connect(&audioInput, &AudioInput::selectedInputDeviceIdChanged,
                     &appState, [&updateAndroidCommunicationModePreference]() {
        updateAndroidCommunicationModePreference();
    });
    QObject::connect(&audioInput, &AudioInput::inputDevicesChanged,
                     &appState, [&updateAndroidCommunicationModePreference]() {
        updateAndroidCommunicationModePreference();
    });
    updateAndroidCommunicationModePreference();
    QElapsedTimer pttPressElapsed;
    static constexpr int kPttOnCueProtectMs = 260;
    QTimer pttRouteEnableTimer;
    pttRouteEnableTimer.setSingleShot(true);
    QTimer pttRouteReleaseTimer;
    pttRouteReleaseTimer.setSingleShot(true);
    pttRouteReleaseTimer.setInterval(1200);

    auto requestRouteEnable = [&appState, &androidPttBridge,
                               &pttRouteEnableTimer, &pttPressElapsed]() {
        if (!appState.pttPressed())
            return;
        if (!pttPressElapsed.isValid())
        {
            androidPttBridge.setPttAudioRouteEnabled(true);
            return;
        }
        const int elapsedMs = static_cast<int>(pttPressElapsed.elapsed());
        if (elapsedMs >= kPttOnCueProtectMs)
        {
            androidPttBridge.setPttAudioRouteEnabled(true);
            return;
        }
        const int remainMs = qMax(1, kPttOnCueProtectMs - elapsedMs);
        pttRouteEnableTimer.start(remainMs);
    };

    updateAndroidBackgroundReceiveService = [&app, &currentServerAddress, &currentServerPort]() {
        const bool appInBackground = (app.applicationState() != Qt::ApplicationActive);
        // Keep foreground service active only while a channel target is configured.
        const bool hasTarget = (!currentServerAddress.isNull() && currentServerPort != 0);
        const bool enable = hasTarget && appInBackground;
        QJniObject::callStaticMethod<void>(
            "com/example/incomudon/IncomUdonActivity",
            "setKeepAliveServiceEnabled",
            "(Z)V",
            static_cast<jboolean>(enable ? 1 : 0));
    };
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &appState, [&updateAndroidBackgroundReceiveService](Qt::ApplicationState) {
        updateAndroidBackgroundReceiveService();
    });
    updateAndroidBackgroundReceiveService();

    QObject::connect(&androidPttBridge, &AndroidPttBridge::headsetButtonChanged,
                     &appState, [&appState](bool pressed) {
        if (!pressed)
            return;
        if (!appState.serverOnline())
            return;
        appState.setPttPressed(!appState.pttPressed());
    });
    QObject::connect(&appState, &AppState::pttPressedChanged,
                     &appState, [&appState, &pttPressElapsed,
                                 &pttRouteEnableTimer, &pttRouteReleaseTimer]() {
        if (appState.pttPressed())
        {
            pttPressElapsed.restart();
            pttRouteEnableTimer.stop();
            pttRouteReleaseTimer.stop();
            return;
        }
        pttRouteEnableTimer.stop();
        pttRouteReleaseTimer.start();
    });
    QObject::connect(&pttRouteEnableTimer, &QTimer::timeout,
                     &appState, [&appState, &androidPttBridge]() {
        if (!appState.pttPressed())
            return;
        androidPttBridge.setPttAudioRouteEnabled(true);
    });
    QObject::connect(&pttController, &PttController::txStarted,
                     &appState, [&requestRouteEnable, &pttRouteReleaseTimer]() {
        pttRouteReleaseTimer.stop();
        requestRouteEnable();
    });
    QObject::connect(&pttController, &PttController::txStopped,
                     &appState, [&appState, &pttRouteReleaseTimer]() {
        if (!appState.pttPressed())
            pttRouteReleaseTimer.start();
    });
    QObject::connect(&pttRouteReleaseTimer, &QTimer::timeout,
                     &appState, [&appState, &pttController, &androidPttBridge]() {
        if (appState.pttPressed() || pttController.pttPressed())
            return;
        androidPttBridge.setPttAudioRouteEnabled(false);
    });
#endif

    auto applyCryptoPreference = [&keyExchange, &appState]() {
        const bool legacy = (appState.cryptoMode() == AppState::CryptoLegacyXor);
        keyExchange.setPreferredMode(legacy ? KeyExchange::CryptoMode::LegacyXor
                                             : KeyExchange::CryptoMode::AesGcm);
    };

    QObject::connect(&appState, &AppState::cryptoModeChanged,
                     &appState, applyCryptoPreference);

    auto applyCodecSettings = [&codecTx, &audioInput, &appState]() {
        codecTx.setMode(appState.codecBitrate());
        const int pcmBytes = codecTx.pcmFrameBytes();
        const int frameMs = codecTx.frameMs();
        if (pcmBytes > 0)
        {
            audioInput.setFrameBytes(pcmBytes);
        }
        if (frameMs > 0)
        {
            audioInput.setIntervalMs(frameMs);
        }
    };

    auto applyCodecSelection = [&codecTx, &appState]() {
        if (appState.codecSelection() == AppState::CodecOpus)
            codecTx.setCodecType(Codec2Wrapper::CodecTypeOpus);
        else
            codecTx.setCodecType(Codec2Wrapper::CodecTypeCodec2);
        codecTx.setForcePcm(appState.forcePcm());
    };

    auto syncCodec2LibraryState = [&appState, &codecTx]() {
        appState.setCodec2LibraryLoaded(codecTx.codec2LibraryLoaded());
        appState.setCodec2LibraryError(codecTx.codec2LibraryError());
    };
    auto syncOpusLibraryState = [&appState, &codecTx]() {
        appState.setOpusLibraryLoaded(codecTx.opusLibraryLoaded());
        appState.setOpusLibraryError(codecTx.opusLibraryError());
    };

    QObject::connect(&appState, &AppState::codec2LibraryPathChanged,
                     &appState, [&appState, &codecTx, &codecRx]() {
        const QString path = appState.codec2LibraryPath();
        codecTx.setCodec2LibraryPath(path);
        codecRx.setCodec2LibraryPath(path);
    });
    QObject::connect(&appState, &AppState::opusLibraryPathChanged,
                     &appState, [&appState, &codecTx, &codecRx]() {
        const QString path = appState.opusLibraryPath();
        codecTx.setOpusLibraryPath(path);
        codecRx.setOpusLibraryPath(path);
    });

    QObject::connect(&codecTx, &Codec2Wrapper::codec2LibraryLoadedChanged,
                     &appState, [&syncCodec2LibraryState, &codecTx]() {
        syncCodec2LibraryState();
        logCodecStatus(QStringLiteral("Codec2 runtime library loaded=%1")
                           .arg(codecTx.codec2LibraryLoaded() ? 1 : 0));
    });
    QObject::connect(&codecTx, &Codec2Wrapper::codec2LibraryErrorChanged,
                     &appState, [&syncCodec2LibraryState]() {
        syncCodec2LibraryState();
    });
    QObject::connect(&codecTx, &Codec2Wrapper::opusLibraryLoadedChanged,
                     &appState, [&syncOpusLibraryState]() {
        syncOpusLibraryState();
    });
    QObject::connect(&codecTx, &Codec2Wrapper::opusLibraryErrorChanged,
                     &appState, [&syncOpusLibraryState]() {
        syncOpusLibraryState();
    });
    QObject::connect(&codecTx, &Codec2Wrapper::pcmFrameBytesChanged,
                     &appState, applyCodecSettings);
    QObject::connect(&codecTx, &Codec2Wrapper::frameMsChanged,
                     &appState, applyCodecSettings);

    QObject::connect(&appState, &AppState::codecBitrateChanged,
                     &appState, [&applyCodecSettings, &codecTx]() {
        applyCodecSettings();
        logCodecStatus(QStringLiteral("TX codec mode=%1 forcePcm=%2 codec2Active=%3 opusActive=%4")
                           .arg(codecTx.mode())
                           .arg(codecTx.forcePcm() ? 1 : 0)
                           .arg(codecTx.codec2Active() ? 1 : 0)
                           .arg(codecTx.opusActive() ? 1 : 0));
    });

    QObject::connect(&appState, &AppState::codecSelectionChanged,
                     &appState, [&applyCodecSelection, &codecTx, &sendCodecConfig]() {
        applyCodecSelection();
        sendCodecConfig(true);
        logCodecStatus(QStringLiteral("TX codec selection changed type=%1 forcePcm=%2 codec2Active=%3 opusActive=%4")
                           .arg(codecTx.codecType())
                           .arg(codecTx.forcePcm() ? 1 : 0)
                           .arg(codecTx.codec2Active() ? 1 : 0)
                           .arg(codecTx.opusActive() ? 1 : 0));
    });

    QObject::connect(&appState, &AppState::forcePcmChanged,
                     &appState, [&codecTx, &appState]() {
        codecTx.setForcePcm(appState.forcePcm());
        logCodecStatus(QStringLiteral("TX codec mode=%1 forcePcm=%2 codec2Active=%3 opusActive=%4")
                           .arg(codecTx.mode())
                           .arg(codecTx.forcePcm() ? 1 : 0)
                           .arg(codecTx.codec2Active() ? 1 : 0)
                           .arg(codecTx.opusActive() ? 1 : 0));
    });

    QObject::connect(&appState, &AppState::micVolumePercentChanged,
                     &appState, [&audioInput, &appState]() {
        audioInput.setInputGainPercent(appState.micVolumePercent());
    });
    QObject::connect(&appState, &AppState::noiseSuppressionEnabledChanged,
                     &appState, [&audioInput, &appState]() {
        audioInput.setNoiseSuppressionEnabled(appState.noiseSuppressionEnabled());
    });
    QObject::connect(&appState, &AppState::noiseSuppressionLevelChanged,
                     &appState, [&audioInput, &appState]() {
        audioInput.setNoiseSuppressionLevel(appState.noiseSuppressionLevel());
    });

    QObject::connect(&appState, &AppState::speakerVolumePercentChanged,
                     &appState, [&audioOutput, &appState]() {
        audioOutput.setOutputGainPercent(appState.speakerVolumePercent());
    });
    QObject::connect(&appState, &AppState::keepMicSessionAlwaysOnChanged,
                     &appState, [&pttController, &appState]() {
        pttController.setAlwaysKeepInputSession(appState.keepMicSessionAlwaysOn());
    });

    QObject::connect(&appState, &AppState::fecEnabledChanged,
                     &appState, [&appState, &pttController, &channelManager]() {
        const bool rxFecAssistAlwaysOn = true;
        pttController.setFecEnabled(appState.fecEnabled());
        channelManager.setFecEnabled(rxFecAssistAlwaysOn);
        logCodecStatus(QStringLiteral("FEC tx=%1 rx=%2")
                           .arg(appState.fecEnabled() ? 1 : 0)
                           .arg(rxFecAssistAlwaysOn ? 1 : 0));
    });
    QObject::connect(&appState, &AppState::qosEnabledChanged,
                     &appState, [&appState, &transport]() {
        transport.setQosEnabled(appState.qosEnabled());
        logCodecStatus(QStringLiteral("Network QoS %1 (DSCP EF)")
                           .arg(appState.qosEnabled() ? QStringLiteral("enabled")
                                                      : QStringLiteral("disabled")));
    });

    sendCodecConfig = [&packetizer, &transport, &currentServerAddress, &currentServerPort,
                       &appState, &codecTx, &suppressCodecBroadcast,
                       &lastSentCodecMode, &lastSentCodecId,
                       &lastSentAddress, &lastSentPort](bool force = false) {
        if (suppressCodecBroadcast)
            return;
        if (currentServerPort == 0 || currentServerAddress.isNull())
            return;

        const int codecId = codecTx.activeCodecTransportId();
        const bool pcmOnly = (codecId == Proto::CODEC_TRANSPORT_PCM);
        if (!force)
        {
            if (lastSentAddress == currentServerAddress &&
                lastSentPort == currentServerPort &&
                lastSentCodecMode == appState.codecBitrate() &&
                lastSentCodecId == codecId)
            {
                return;
            }
        }
        QByteArray payload(4, 0);
        payload[0] = static_cast<char>(pcmOnly ? 1 : 0);
        payload[1] = static_cast<char>(codecId);
        const quint16 mode = static_cast<quint16>(appState.codecBitrate());
        qToBigEndian(mode, reinterpret_cast<uchar*>(payload.data() + 2));

        const QByteArray packet = packetizer.packPlain(Proto::PKT_CODEC_CONFIG, payload);
        transport.send(packet, currentServerAddress, currentServerPort);
        logCodecStatus(QStringLiteral("TX codec_config sent mode=%1 codecId=%2 forcePcm=%3 codec2Active=%4 opusActive=%5")
                           .arg(appState.codecBitrate())
                           .arg(codecId)
                           .arg(pcmOnly ? 1 : 0)
                           .arg(codecTx.codec2Active() ? 1 : 0)
                           .arg(codecTx.opusActive() ? 1 : 0));

        lastSentAddress = currentServerAddress;
        lastSentPort = currentServerPort;
        lastSentCodecMode = appState.codecBitrate();
        lastSentCodecId = codecId;
    };

    QObject::connect(&appState, &AppState::senderIdChanged,
                     &appState, [&appState, &packetizer,
                                 &lastSentAddress, &lastSentPort,
                                 &lastSentCodecMode, &lastSentCodecId,
                                 &sendCodecConfig]() {
        const quint32 senderId = appState.senderId();
        if (senderId == 0)
            return;
        packetizer.setSenderId(senderId);
        appState.setSelfId(senderId);
        lastSentAddress = QHostAddress();
        lastSentPort = 0;
        lastSentCodecMode = -1;
        lastSentCodecId = -1;
        sendCodecConfig(true);
    });

    QObject::connect(&appState, &AppState::codecBitrateChanged,
                     &appState, [&sendCodecConfig]() {
        sendCodecConfig(false);
    });
    QObject::connect(&appState, &AppState::forcePcmChanged,
                     &appState, [&sendCodecConfig]() {
        sendCodecConfig(false);
    });
    QObject::connect(&codecTx, &Codec2Wrapper::codec2ActiveChanged,
                     &appState, [&applyCodecSettings, &sendCodecConfig, &codecTx]() {
        applyCodecSettings();
        sendCodecConfig(true);
        logCodecStatus(QStringLiteral("TX codec active changed codec2=%1 opus=%2")
                           .arg(codecTx.codec2Active() ? 1 : 0)
                           .arg(codecTx.opusActive() ? 1 : 0));
    });
    QObject::connect(&codecTx, &Codec2Wrapper::opusActiveChanged,
                     &appState, [&applyCodecSettings, &sendCodecConfig, &codecTx]() {
        applyCodecSettings();
        sendCodecConfig(true);
        logCodecStatus(QStringLiteral("TX codec active changed codec2=%1 opus=%2")
                           .arg(codecTx.codec2Active() ? 1 : 0)
                           .arg(codecTx.opusActive() ? 1 : 0));
    });

    QObject::connect(&channelManager, &ChannelManager::talkerChanged,
                     &appState, [&appState](quint32 talkerId) {
        appState.setTalkerId(talkerId);
    });

    QObject::connect(&channelManager, &ChannelManager::talkerChanged,
                     &pttController, [&packetizer, &pttController](quint32 talkerId) {
        const bool allowed = (talkerId == packetizer.senderId());
        pttController.setTalkAllowed(allowed);
    });

    QObject::connect(&channelManager, &ChannelManager::talkerChanged,
                     &pttController, [&packetizer, &pttController](quint32 talkerId) {
        const bool remoteRxActive = (talkerId != 0 && talkerId != packetizer.senderId());
        if (remoteRxActive)
            pttController.setRxHoldActive(true);
    });
    QObject::connect(&channelManager, &ChannelManager::talkReleasePlayoutCompleted,
                     &pttController, [&packetizer, &pttController](quint32 talkerId) {
        if (talkerId == 0 || talkerId == packetizer.senderId())
            return;
        pttController.setRxHoldActive(false);
    });

    QObject::connect(&channelManager, &ChannelManager::talkDenied,
                     &appState, [&appState](quint32 currentTalkerId) {
        appState.setLinkStatus(QStringLiteral("Busy: ") + QString::number(currentTalkerId));
    });

    QObject::connect(&channelManager, &ChannelManager::codecConfigReceived,
                     &appState, [&codecRx,
                                  &suppressCodecBroadcast,
                                  &lastRxConfigSender,
                                  &lastRxConfigMode,
                                  &lastRxConfigCodecId]
                                  (quint32 senderId, int mode, bool pcmOnly, int codecId) {
        int normalizedCodecId = codecId;
        if (pcmOnly || codecId == Proto::CODEC_TRANSPORT_PCM)
            normalizedCodecId = Proto::CODEC_TRANSPORT_PCM;
        else if (codecId != Proto::CODEC_TRANSPORT_CODEC2 &&
                 codecId != Proto::CODEC_TRANSPORT_OPUS)
            normalizedCodecId = Proto::CODEC_TRANSPORT_CODEC2;

        if (lastRxConfigSender == senderId &&
            lastRxConfigMode == mode &&
            lastRxConfigCodecId == normalizedCodecId)
        {
            return;
        }
        logCodecStatus(QStringLiteral("RX codec_config recv sender=%1 mode=%2 codecId=%3 pcmOnly=%4")
                           .arg(senderId)
                           .arg(mode)
                           .arg(normalizedCodecId)
                           .arg(pcmOnly ? 1 : 0));
        suppressCodecBroadcast = true;
        if (normalizedCodecId == Proto::CODEC_TRANSPORT_OPUS)
            codecRx.setCodecType(Codec2Wrapper::CodecTypeOpus);
        else
            codecRx.setCodecType(Codec2Wrapper::CodecTypeCodec2);
        codecRx.setForcePcm(normalizedCodecId == Proto::CODEC_TRANSPORT_PCM);
        codecRx.setMode(mode);
        logCodecStatus(QStringLiteral("RX codec_config applied mode=%1 codecId=%2 forcePcm=%3 codec2Active=%4 opusActive=%5")
                           .arg(mode)
                           .arg(normalizedCodecId)
                           .arg(codecRx.forcePcm() ? 1 : 0)
                           .arg(codecRx.codec2Active() ? 1 : 0)
                           .arg(codecRx.opusActive() ? 1 : 0));
        suppressCodecBroadcast = false;
        lastRxConfigSender = senderId;
        lastRxConfigMode = mode;
        lastRxConfigCodecId = normalizedCodecId;
    });

    QObject::connect(&channelManager, &ChannelManager::handshakeReceived,
                     &keyExchange, &KeyExchange::processHandshakePacket);
    QObject::connect(&channelManager, &ChannelManager::channelError,
                     &appState, [&appState, &pttController, &txActive](const QString& message) {
        txActive = false;
        appState.setLinkStatus(message);
        appState.setServerOnline(false);
        appState.setTalkerId(0);
        pttController.setTalkAllowed(false);
        pttController.setRxHoldActive(false);
    });

    QTimer serverTimeout;
    serverTimeout.setSingleShot(true);

    QTimer codecConfigTimer;
    codecConfigTimer.setInterval(1000);
    codecConfigTimer.setTimerType(Qt::CoarseTimer);
    QObject::connect(&codecConfigTimer, &QTimer::timeout,
                     &appState, [&sendCodecConfig]() {
        sendCodecConfig(true);
    });

    QTimer keepaliveTimer;
    keepaliveTimer.setInterval(5000);
    keepaliveTimer.setTimerType(Qt::CoarseTimer);
    QObject::connect(&keepaliveTimer, &QTimer::timeout,
                     &appState, [&packetizer, &transport,
                                 &currentServerAddress, &currentServerPort]() {
        if (currentServerPort == 0 || currentServerAddress.isNull())
            return;
        const QByteArray packet = packetizer.packPlain(Proto::PKT_KEEPALIVE, QByteArray());
        transport.send(packet, currentServerAddress, currentServerPort);
    });

    QObject::connect(&channelManager, &ChannelManager::channelConfigured,
                     &appState,
                     [&appState, &pttController, &keyExchange, &cipher, &serverTimeout,
                       &applyCryptoPreference, &sendCodecConfig, &keepaliveTimer, &codecConfigTimer,
                       &lastRxConfigSender, &lastRxConfigMode, &lastRxConfigCodecId,
                       &currentServerAddress, &currentServerPort,
                       &updateAndroidBackgroundReceiveService, &txActive](quint32 channelId,
                                                                  const QString& address,
                                                                  quint16 port,
                                                                  const QString& password) {
        appState.setCurrentChannelId(static_cast<int>(channelId));
        appState.setLinkStatus(QStringLiteral("Connecting..."));
        txActive = false;
        appState.setServerOnline(false);
        appState.setTalkerId(0);
        appState.setPttPressed(false);
        lastRxConfigSender = 0;
        lastRxConfigMode = -1;
        lastRxConfigCodecId = -1;
        pttController.setTalkAllowed(false);
        pttController.setRxHoldActive(false);

        currentServerAddress = QHostAddress(address);
        currentServerPort = port;
        pttController.setTarget(currentServerAddress, currentServerPort);
        updateAndroidBackgroundReceiveService();

        applyCryptoPreference();
        cipher.setKey(QByteArray(), QByteArray());

        keyExchange.setChannelId(channelId);
        keyExchange.setPassword(password);
        logCodecStatus(QStringLiteral("Channel configured: starting key exchange"));
        keyExchange.startHandshake();
        logCodecStatus(QStringLiteral("Channel configured: key exchange started"));

        serverTimeout.start(8000);
        sendCodecConfig(true);
        logCodecStatus(QStringLiteral("Channel configured: codec config sent"));
        if (!keepaliveTimer.isActive())
            keepaliveTimer.start();
        if (codecConfigTimer.isActive())
            codecConfigTimer.stop();
        logCodecStatus(QStringLiteral("Channel configured id=%1 addr=%2 port=%3")
                           .arg(channelId)
                           .arg(address)
                           .arg(port));
    });

    QObject::connect(&channelManager, &ChannelManager::targetChanged,
                     &appState, [&channelManager, &pttController,
                                  &keepaliveTimer, &codecConfigTimer,
                                  &currentServerAddress, &currentServerPort,
                                  &updateAndroidBackgroundReceiveService]() {
        const QString address = channelManager.targetAddress();
        if (address.isEmpty())
        {
            currentServerAddress = QHostAddress();
            currentServerPort = 0;
            pttController.setTarget(QHostAddress(), 0);
            keepaliveTimer.stop();
            codecConfigTimer.stop();
            updateAndroidBackgroundReceiveService();
            return;
        }

        currentServerAddress = QHostAddress(address);
        currentServerPort = channelManager.targetPort();
        pttController.setTarget(currentServerAddress, currentServerPort);
        updateAndroidBackgroundReceiveService();
        if (!keepaliveTimer.isActive())
            keepaliveTimer.start();
    });

    QObject::connect(&channelManager, &ChannelManager::serverActivity,
                     &appState, [&appState, &serverTimeout, &txActive]() {
        serverTimeout.stop();
        appState.setServerOnline(true);
        if (txActive)
            appState.setLinkStatus(QStringLiteral("TX"));
        else
            appState.setLinkStatus(QStringLiteral("Ready"));
    });

    QObject::connect(&serverTimeout, &QTimer::timeout,
                     &appState, [&appState, &pttController, &txActive]() {
        txActive = false;
        appState.setServerOnline(false);
        appState.setLinkStatus(QStringLiteral("No response"));
        appState.setTalkerId(0);
        pttController.setTalkAllowed(false);
        pttController.setRxHoldActive(false);
    });

    QObject::connect(&pttController, &PttController::txStarted,
                     &appState, [&codecConfigTimer, &sendCodecConfig]() {
        sendCodecConfig(true);
        if (!codecConfigTimer.isActive())
            codecConfigTimer.start();
    });
    QObject::connect(&pttController, &PttController::txStopped,
                     &appState, [&codecConfigTimer]() {
        codecConfigTimer.stop();
    });

    QObject::connect(&transport, &UdpTransport::bindFailed,
                     &appState, [&appState](const QString& error) {
        appState.setLinkStatus(QStringLiteral("Bind failed: ") + error);
    });

    // Bind to an ephemeral local UDP port so each client can use a different
    // source port; the relay tracks sender endpoints dynamically.
    transport.setQosEnabled(appState.qosEnabled());
    transport.bind(0);

    quint32 senderId = appState.senderId();
    if (senderId == 0)
        senderId = QRandomGenerator::global()->bounded(1u, 0x7FFFFFFFu);
    appState.setSenderId(senderId);
    packetizer.setSenderId(senderId);
    packetizer.setKeyId(cipher.keyId());
    appState.setSelfId(senderId);

    channelManager.setTransport(&transport);
    channelManager.setPacketizer(&packetizer);
    channelManager.setCipher(&cipher);
    channelManager.setJitterBuffer(&jitter);
    channelManager.setCodec(&codecRx);
    channelManager.setAudioOutput(&audioOutput);
    channelManager.setFecEnabled(true);

    pttController.setAudioInput(&audioInput);
    pttController.setCodec(&codecTx);
    pttController.setCipher(&cipher);
    pttController.setPacketizer(&packetizer);
    pttController.setTransport(&transport);
    pttController.setFecEnabled(appState.fecEnabled());
    pttController.setAlwaysKeepInputSession(appState.keepMicSessionAlwaysOn());

    applyCodecSettings();
    codecTx.setCodec2LibraryPath(appState.codec2LibraryPath());
    codecRx.setCodec2LibraryPath(appState.codec2LibraryPath());
    syncCodec2LibraryState();
    codecTx.setOpusLibraryPath(appState.opusLibraryPath());
    codecRx.setOpusLibraryPath(appState.opusLibraryPath());
    syncOpusLibraryState();
    applyCodecSelection();
    codecTx.setForcePcm(appState.forcePcm());
    codecRx.setMode(appState.codecBitrate());
    codecRx.setForcePcm(appState.forcePcm());
    audioInput.setInputGainPercent(appState.micVolumePercent());
    audioInput.setNoiseSuppressionEnabled(appState.noiseSuppressionEnabled());
    audioInput.setNoiseSuppressionLevel(appState.noiseSuppressionLevel());
    audioOutput.setOutputGainPercent(appState.speakerVolumePercent());
    logCodecStatus(QStringLiteral("Initial TX codec mode=%1 type=%2 forcePcm=%3 codec2Active=%4 opusActive=%5")
                       .arg(codecTx.mode())
                       .arg(codecTx.codecType())
                       .arg(codecTx.forcePcm() ? 1 : 0)
                       .arg(codecTx.codec2Active() ? 1 : 0)
                       .arg(codecTx.opusActive() ? 1 : 0));
    logCodecStatus(QStringLiteral("Initial RX codec mode=%1 type=%2 forcePcm=%3 codec2Active=%4 opusActive=%5")
                       .arg(codecRx.mode())
                       .arg(codecRx.codecType())
                       .arg(codecRx.forcePcm() ? 1 : 0)
                       .arg(codecRx.codec2Active() ? 1 : 0)
                       .arg(codecRx.opusActive() ? 1 : 0));

    engine.rootContext()->setContextProperty("appState", &appState);
    engine.rootContext()->setContextProperty("audioInput", &audioInput);
    engine.rootContext()->setContextProperty("pttController", &pttController);
    engine.rootContext()->setContextProperty("channelManager", &channelManager);
    engine.rootContext()->setContextProperty("androidPttBridge", &androidPttBridge);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("IncomUdon", "Main");

    return app.exec();
}
