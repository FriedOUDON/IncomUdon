import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import QtMultimedia
import QtCore

Window {
    width: 360
    height: 640
    visible: true
    title: "IncomUdon"

    Rectangle {
        id: root
        anchors.fill: parent
        color: "#0f141a"

        property string serverAddress: ""
        property string serverPort: "50000"
        property string channelId: "100"
        property string password: ""
        property var codec2ModeOptions: [450, 700, 1600, 2400, 3200]
        property var opusBpsOptions: [6000, 8000, 12000, 16000, 20000, 64000, 96000, 128000]
        property url defaultPttOnSoundUrl: "qrc:/qt/qml/IncomUdon/assets/sfx/ptt_on.wav"
        property url defaultPttOffSoundUrl: "qrc:/qt/qml/IncomUdon/assets/sfx/ptt_off.wav"
        property url defaultCarrierSenseSoundUrl: "qrc:/qt/qml/IncomUdon/assets/sfx/carrier_sense.wav"
        property url pttOnSoundUrl: defaultPttOnSoundUrl
        property url pttOffSoundUrl: defaultPttOffSoundUrl
        property url carrierSenseSoundUrl: defaultCarrierSenseSoundUrl
        property int cueVolumePercent: 50
        property bool pttOnSoundEnabled: false
        property bool pttOffSoundEnabled: true
        property bool carrierSenseSoundEnabled: true
        property string cuePickerTarget: ""
        property bool pttOnCuePendingA: false
        property bool pttOnCuePendingB: false
        property bool pttOffCuePendingA: false
        property bool pttOffCuePendingB: false
        property bool carrierCuePending: false
        property bool codec2Selectable: appState.codec2LibraryPath.length > 0 &&
                                        appState.codec2LibraryLoaded
        property bool opusSelectable: appState.opusAvailable
        property bool pttOnVoiceFlip: false
        property bool pttOffVoiceFlip: false
        property double lastCarrierCueMs: 0
        property bool pttOnPlayedForCurrentTx: false
        property int pttOnPlayRequestId: 0
        property int pttOnPlayStartedId: 0
        property int pttOnRetryAttempts: 0
        property int pttOnCueRecoveryRequestId: 0
        property double lastCueRecoveryMs: 0
        property bool suppressForcePcmPersistence: false
        property var cueAudioDevice: cueMediaDevices.defaultAudioOutput
        property string codec2DownloadUrl: "https://github.com/FriedOUDON/libcodec2/releases/tag/v1.0.2"
        property string licensesText: ""
        readonly property real _pxPerDp: Math.max(1.0, Screen.pixelDensity * 25.4 / 160.0)
        readonly property int _androidTopInsetFallback: Qt.platform.os === "android" ? Math.round(28 * _pxPerDp) : 0
        readonly property int _androidBottomInsetFallback: Qt.platform.os === "android" ? Math.round(56 * _pxPerDp) : 0
        readonly property int safeInsetTop: Math.max(SafeArea.margins.top, _androidTopInsetFallback)
        readonly property int safeInsetBottom: Math.max(SafeArea.margins.bottom, _androidBottomInsetFallback)
        readonly property int safeInsetLeft: SafeArea.margins.left
        readonly property int safeInsetRight: SafeArea.margins.right

        function clampInt(v, minValue, maxValue, fallback) {
            var n = parseInt(v)
            if (isNaN(n))
                n = fallback
            if (n < minValue)
                n = minValue
            if (n > maxValue)
                n = maxValue
            return n
        }

        function parseSenderIdInput(v) {
            var textValue = (v === undefined || v === null) ? "" : v.toString().trim()
            if (textValue.length === 0)
                return 0
            var n = Number(textValue)
            if (!isFinite(n))
                return 0
            if (Math.floor(n) !== n)
                return 0
            if (n < 1 || n > 2147483647)
                return 0
            return n
        }

        function fileNameFromUrl(urlValue) {
            if (!urlValue || urlValue.toString().length === 0)
                return "(none)"
            var s = urlValue.toString()
            s = s.replace(/\\/g, "/")
            var q = s.indexOf("?")
            if (q >= 0)
                s = s.slice(0, q)
            var idx = s.lastIndexOf("/")
            if (idx >= 0 && idx + 1 < s.length)
                return decodeURIComponent(s.slice(idx + 1))
            return s
        }

        function loadResourceTextAsync(urlCandidates, callback) {
            var candidates = urlCandidates || []
            if (candidates.length === 0) {
                callback("No resource candidates")
                return
            }

            var idx = 0
            function tryNext(lastError) {
                if (idx >= candidates.length) {
                    callback("Failed to read resource\n" + lastError)
                    return
                }

                var urlValue = candidates[idx++]
                var xhr = new XMLHttpRequest()
                xhr.onreadystatechange = function() {
                    if (xhr.readyState !== XMLHttpRequest.DONE)
                        return
                    if (xhr.status === 200 || xhr.status === 0) {
                        callback(xhr.responseText)
                        return
                    }
                    tryNext("url=" + urlValue + " status=" + xhr.status)
                }
                xhr.onerror = function() {
                    tryNext("url=" + urlValue + " network error")
                }
                try {
                    xhr.open("GET", urlValue)
                    xhr.send()
                } catch (e) {
                    tryNext("url=" + urlValue + " " + e)
                }
            }
            tryNext("")
        }

        function refreshLicensesText() {
            licensesText = "Loading licenses..."

            var appLicense = ""
            var notices = ""
            var apache = ""
            var lgpl = ""
            var pending = 4

            function completeOne() {
                pending = pending - 1
                if (pending > 0)
                    return
                licensesText =
                        "=== App License (LICENSE) ===\n" + appLicense +
                        "\n\n=== Third Party Notices ===\n" + notices +
                        "\n\n=== Apache-2.0 ===\n" + apache +
                        "\n\n=== LGPL-2.1 ===\n" + lgpl
            }

            loadResourceTextAsync([
                                      Qt.resolvedUrl("../LICENSE"),
                                      "qrc:/qt/qml/IncomUdon/LICENSE",
                                      "qrc:/LICENSE"
                                  ], function(text) {
                                      appLicense = text
                                      completeOne()
                                  })
            loadResourceTextAsync([
                                      Qt.resolvedUrl("../THIRD_PARTY_NOTICES.md"),
                                      "qrc:/qt/qml/IncomUdon/THIRD_PARTY_NOTICES.md",
                                      "qrc:/THIRD_PARTY_NOTICES.md"
                                  ], function(text) {
                                      notices = text
                                      completeOne()
                                  })
            loadResourceTextAsync([
                                      Qt.resolvedUrl("../LICENSES/Apache-2.0.txt"),
                                      "qrc:/qt/qml/IncomUdon/LICENSES/Apache-2.0.txt",
                                      "qrc:/LICENSES/Apache-2.0.txt"
                                  ], function(text) {
                                      apache = text
                                      completeOne()
                                  })
            loadResourceTextAsync([
                                      Qt.resolvedUrl("../LICENSES/LGPL-2.1.txt"),
                                      "qrc:/qt/qml/IncomUdon/LICENSES/LGPL-2.1.txt",
                                      "qrc:/LICENSES/LGPL-2.1.txt"
                                  ], function(text) {
                                      lgpl = text
                                      completeOne()
                                  })
        }

        function nearestValue(model, value) {
            var best = model[0]
            var bestDiff = Math.abs(value - best)
            for (var i = 1; i < model.length; ++i) {
                var diff = Math.abs(value - model[i])
                if (diff < bestDiff) {
                    bestDiff = diff
                    best = model[i]
                }
            }
            return best
        }

        function normalizeBitrate(value) {
            var best = codec2ModeOptions[0]
            var bestDiff = Math.abs(value - best)
            for (var i = 1; i < codec2ModeOptions.length; ++i) {
                var diff = Math.abs(value - codec2ModeOptions[i])
                if (diff < bestDiff) {
                    bestDiff = diff
                    best = codec2ModeOptions[i]
                }
            }
            return best
        }

        function bitrateLabel(value, selection) {
            var n = parseInt(value)
            if (isNaN(n))
                n = 0
            if (selection === 2)
                return Math.round(n / 1000) + " kbps"
            return n + " bps"
        }

        function legacyModeToOpusBps(mode) {
            if (mode <= 450)
                return 6000
            if (mode <= 700)
                return 8000
            if (mode <= 1600)
                return 12000
            if (mode <= 2400)
                return 16000
            return 20000
        }

        function normalizeBitrateForSelection(value, selection) {
            if (selection === 2) {
                var target = value
                if (target < 6000)
                    target = legacyModeToOpusBps(target)
                return nearestValue(opusBpsOptions, target)
            }
            return nearestValue(codec2ModeOptions, value)
        }

        function bitrateModelForSelection(selection) {
            return selection === 2 ? opusBpsOptions : codec2ModeOptions
        }

        function bitrateToIndex(storedMode, selection) {
            var model = bitrateModelForSelection(selection)
            var targetValue = normalizeBitrateForSelection(storedMode, selection)
            for (var i = 0; i < model.length; ++i) {
                if (model[i] === targetValue)
                    return i
            }
            return 0
        }

        function indexToBitrate(idx, selection) {
            var model = bitrateModelForSelection(selection)
            if (idx < 0)
                idx = 0
            if (idx >= model.length)
                idx = model.length - 1
            return model[idx]
        }

        function isLikelyBluetoothName(name) {
            if (!name || name.length === 0)
                return false
            var n = name.toLowerCase()
            return n.indexOf("bluetooth") >= 0 ||
                   n.indexOf("ble") >= 0 ||
                   n.indexOf("headset") >= 0 ||
                   n.indexOf("hands-free") >= 0 ||
                   n.indexOf("wireless") >= 0
        }

        function selectedMicDeviceName() {
            var names = audioInput.inputDeviceNames
            var ids = audioInput.inputDeviceIds
            if (!names || names.length === 0)
                return ""

            var index = 0
            var selectedId = audioInput.selectedInputDeviceId
            if (selectedId && selectedId.length > 0 && ids) {
                var found = ids.indexOf(selectedId)
                if (found >= 0)
                    index = found
            }

            if (index < 0 || index >= names.length)
                return ""
            return names[index]
        }

        function syncCueAudioDevice() {
            var target = cueMediaDevices.defaultAudioOutput
            if (isLikelyBluetoothName(selectedMicDeviceName())) {
                var outputs = cueMediaDevices.audioOutputs
                if (outputs && outputs.length > 0) {
                    for (var i = 0; i < outputs.length; ++i) {
                        var desc = outputs[i].description
                        if (isLikelyBluetoothName(desc)) {
                            target = outputs[i]
                            break
                        }
                    }
                }
            }
            root.cueAudioDevice = target
        }

        function useAndroidCueToneFallback() {
            return Qt.platform.os === "android" && isLikelyBluetoothName(selectedMicDeviceName())
        }

        function playAndroidCueTone(cueId, enabled) {
            if (!enabled)
                return
            if (!useAndroidCueToneFallback())
                return
            if (!androidPttBridge)
                return
            androidPttBridge.playCueTone(cueId)
        }

        function syncCodec2Availability() {
            if (appState.codecSelection === 1 && !root.codec2Selectable) {
                root.suppressForcePcmPersistence = true
                appState.codecSelection = root.opusSelectable ? 2 : 0
                root.suppressForcePcmPersistence = false
                return
            }
            if (appState.codecSelection === 2 && !root.opusSelectable) {
                root.suppressForcePcmPersistence = true
                appState.codecSelection = root.codec2Selectable ? 1 : 0
                root.suppressForcePcmPersistence = false
            }
        }

        function micDeviceIndexFromId(deviceId) {
            var ids = audioInput.inputDeviceIds
            if (!ids || ids.length === 0)
                return 0
            for (var i = 0; i < ids.length; ++i) {
                if (ids[i] === deviceId)
                    return i
            }
            return 0
        }

        Settings {
            id: persisted
            category: "ui"
            property string serverAddress: ""
            property string serverPort: "50000"
            property int senderId: 0
            property string channelId: "100"
            property string password: ""
            property int codecSelection: 0
            property int codecBitrate: 1600
            property bool forcePcm: true
            property bool txFecEnabled: true
            property bool qosEnabled: true
            property int cryptoMode: 0
            property int pageIndex: 0
            property int micVolumePercent: 100
            property bool noiseSuppressionEnabled: false
            property int noiseSuppressionLevel: 45
            property int speakerVolumePercent: 100
            property bool keepMicSessionAlwaysOn: false
            property int cueVolumePercent: 50
            property bool pttOnSoundEnabled: false
            property bool pttOffSoundEnabled: true
            property bool carrierSenseSoundEnabled: true
            property string pttOnSoundUrl: ""
            property string pttOffSoundUrl: ""
            property string carrierSenseSoundUrl: ""
            property string codec2LibraryPath: ""
            property string opusLibraryPath: ""
            property string micInputDeviceId: ""
        }

        Component.onCompleted: {
            root.serverAddress = persisted.serverAddress
            root.serverPort = persisted.serverPort
            var storedSenderId = root.clampInt(persisted.senderId, 0, 2147483647, 0)
            if (storedSenderId > 0) {
                appState.senderId = storedSenderId
            } else if (appState.senderId > 0) {
                persisted.senderId = appState.senderId
            }
            root.channelId = persisted.channelId
            root.password = persisted.password

            var initialCodecSelection = root.clampInt(
                        persisted.codecSelection,
                        0,
                        2,
                        persisted.forcePcm ? 0 : 1)
            appState.codecSelection = initialCodecSelection
            appState.codecBitrate = root.normalizeBitrateForSelection(
                        persisted.codecBitrate,
                        initialCodecSelection)
            appState.fecEnabled = persisted.txFecEnabled
            appState.qosEnabled = persisted.qosEnabled
            appState.cryptoMode = appState.opensslAvailable ? persisted.cryptoMode : 1
            appState.micVolumePercent = root.clampInt(persisted.micVolumePercent, 0, 200, 100)
            appState.noiseSuppressionEnabled = persisted.noiseSuppressionEnabled
            appState.noiseSuppressionLevel = root.clampInt(persisted.noiseSuppressionLevel, 0, 100, 45)
            appState.speakerVolumePercent = root.clampInt(persisted.speakerVolumePercent, 0, 400, 100)
            appState.keepMicSessionAlwaysOn = persisted.keepMicSessionAlwaysOn
            root.cueVolumePercent = root.clampInt(persisted.cueVolumePercent, 0, 100, 50)
            root.pttOnSoundEnabled = persisted.pttOnSoundEnabled
            root.pttOffSoundEnabled = persisted.pttOffSoundEnabled
            root.carrierSenseSoundEnabled = persisted.carrierSenseSoundEnabled
            root.pttOnSoundUrl = persisted.pttOnSoundUrl.length > 0 ?
                                     persisted.pttOnSoundUrl : root.defaultPttOnSoundUrl
            root.pttOffSoundUrl = persisted.pttOffSoundUrl.length > 0 ?
                                      persisted.pttOffSoundUrl : root.defaultPttOffSoundUrl
            root.carrierSenseSoundUrl = persisted.carrierSenseSoundUrl.length > 0 ?
                                            persisted.carrierSenseSoundUrl : root.defaultCarrierSenseSoundUrl
            appState.codec2LibraryPath = persisted.codec2LibraryPath
            appState.opusLibraryPath = persisted.opusLibraryPath
            audioInput.selectedInputDeviceId = persisted.micInputDeviceId

            root.syncCodec2Availability()
            root.syncCueAudioDevice()

            tabs.currentIndex = Math.max(0, Math.min(1, persisted.pageIndex))
            root.refreshLicensesText()
            cueWarmupTimer.start()
        }

        onServerAddressChanged: persisted.serverAddress = serverAddress
        onServerPortChanged: persisted.serverPort = serverPort
        onChannelIdChanged: persisted.channelId = channelId
        onPasswordChanged: persisted.password = password
        onPttOnSoundEnabledChanged: persisted.pttOnSoundEnabled = pttOnSoundEnabled
        onPttOffSoundEnabledChanged: persisted.pttOffSoundEnabled = pttOffSoundEnabled
        onCarrierSenseSoundEnabledChanged: persisted.carrierSenseSoundEnabled = carrierSenseSoundEnabled
        onCueVolumePercentChanged: persisted.cueVolumePercent = cueVolumePercent
        onPttOnSoundUrlChanged: persisted.pttOnSoundUrl = pttOnSoundUrl.toString()
        onPttOffSoundUrlChanged: persisted.pttOffSoundUrl = pttOffSoundUrl.toString()
        onCarrierSenseSoundUrlChanged: persisted.carrierSenseSoundUrl = carrierSenseSoundUrl.toString()

        Connections {
            target: appState
            function onSenderIdChanged() { persisted.senderId = appState.senderId }
            function onCodecSelectionChanged() { persisted.codecSelection = appState.codecSelection }
            function onCodecBitrateChanged() {
                persisted.codecBitrate = appState.codecBitrate
            }
            function onForcePcmChanged() {
                if (!root.suppressForcePcmPersistence)
                    persisted.forcePcm = appState.forcePcm
            }
            function onFecEnabledChanged() { persisted.txFecEnabled = appState.fecEnabled }
            function onQosEnabledChanged() { persisted.qosEnabled = appState.qosEnabled }
            function onCryptoModeChanged() { persisted.cryptoMode = appState.cryptoMode }
            function onMicVolumePercentChanged() { persisted.micVolumePercent = appState.micVolumePercent }
            function onNoiseSuppressionEnabledChanged() { persisted.noiseSuppressionEnabled = appState.noiseSuppressionEnabled }
            function onNoiseSuppressionLevelChanged() { persisted.noiseSuppressionLevel = appState.noiseSuppressionLevel }
            function onSpeakerVolumePercentChanged() { persisted.speakerVolumePercent = appState.speakerVolumePercent }
            function onKeepMicSessionAlwaysOnChanged() { persisted.keepMicSessionAlwaysOn = appState.keepMicSessionAlwaysOn }
            function onCodec2LibraryPathChanged() {
                persisted.codec2LibraryPath = appState.codec2LibraryPath
                root.syncCodec2Availability()
            }
            function onCodec2LibraryLoadedChanged() { root.syncCodec2Availability() }
            function onOpusLibraryPathChanged() { persisted.opusLibraryPath = appState.opusLibraryPath }
        }

        Connections {
            target: audioInput
            function onSelectedInputDeviceIdChanged() {
                persisted.micInputDeviceId = audioInput.selectedInputDeviceId
                root.syncCueAudioDevice()
                root.recoverCueEngine(true)
            }
            function onInputDevicesChanged() {
                root.syncCueAudioDevice()
                root.recoverCueEngine()
            }
        }

        SoundEffect {
            id: pttOnCueA
            property int playRequestId: 0
            source: root.pttOnSoundUrl
            audioDevice: root.cueAudioDevice
            volume: root.cueVolumePercent / 100.0
            onStatusChanged: root.flushPendingCue(pttOnCueA, "pttOnCuePendingA")
            onPlayingChanged: {
                if (playing && playRequestId > root.pttOnPlayStartedId)
                    root.pttOnPlayStartedId = playRequestId
            }
        }

        SoundEffect {
            id: pttOnCueB
            property int playRequestId: 0
            source: root.pttOnSoundUrl
            audioDevice: root.cueAudioDevice
            volume: root.cueVolumePercent / 100.0
            onStatusChanged: root.flushPendingCue(pttOnCueB, "pttOnCuePendingB")
            onPlayingChanged: {
                if (playing && playRequestId > root.pttOnPlayStartedId)
                    root.pttOnPlayStartedId = playRequestId
            }
        }

        SoundEffect {
            id: pttOffCueA
            source: root.pttOffSoundUrl
            audioDevice: root.cueAudioDevice
            volume: root.cueVolumePercent / 100.0
            onStatusChanged: root.flushPendingCue(pttOffCueA, "pttOffCuePendingA")
        }

        SoundEffect {
            id: pttOffCueB
            source: root.pttOffSoundUrl
            audioDevice: root.cueAudioDevice
            volume: root.cueVolumePercent / 100.0
            onStatusChanged: root.flushPendingCue(pttOffCueB, "pttOffCuePendingB")
        }

        SoundEffect {
            id: carrierSenseCue
            source: root.carrierSenseSoundUrl
            audioDevice: root.cueAudioDevice
            volume: root.cueVolumePercent / 100.0
            onStatusChanged: root.flushPendingCue(carrierSenseCue, "carrierCuePending")
        }

        function requestCue(effect, enabled, defaultSource, pendingKey, requestId) {
            if (!enabled)
                return
            if (requestId === undefined)
                requestId = 0
            if (effect.playRequestId !== undefined)
                effect.playRequestId = requestId

            if (effect.status === SoundEffect.Ready) {
                effect.stop()
                effect.play()
                root[pendingKey] = false
                return
            }

            if (effect.status === SoundEffect.Error) {
                // Force a reload even if the source already points to default.
                effect.source = ""
                effect.source = defaultSource
                root[pendingKey] = true
                return
            }

            root[pendingKey] = true
        }

        function flushPendingCue(effect, pendingKey) {
            if (!root[pendingKey])
                return
            if (effect.status !== SoundEffect.Ready)
                return
            root[pendingKey] = false
            effect.stop()
            effect.play()
        }

        function reloadCueEffect(effect, sourceUrl) {
            effect.stop()
            effect.source = ""
            effect.source = sourceUrl
        }

        function recoverCueEngine(force) {
            if (force === undefined)
                force = false
            var nowMs = Date.now()
            if (!force && (nowMs - root.lastCueRecoveryMs) < 250)
                return
            root.lastCueRecoveryMs = nowMs

            root.pttOnCuePendingA = false
            root.pttOnCuePendingB = false
            root.pttOffCuePendingA = false
            root.pttOffCuePendingB = false
            root.carrierCuePending = false

            root.reloadCueEffect(pttOnCueA, root.pttOnSoundUrl)
            root.reloadCueEffect(pttOnCueB, root.pttOnSoundUrl)
            root.reloadCueEffect(pttOffCueA, root.pttOffSoundUrl)
            root.reloadCueEffect(pttOffCueB, root.pttOffSoundUrl)
            root.reloadCueEffect(carrierSenseCue, root.carrierSenseSoundUrl)
        }

        function requestPttOnCue(enabled, requestId) {
            root.playAndroidCueTone(1, enabled)
            if (root.pttOnVoiceFlip) {
                root.requestCue(pttOnCueA,
                                enabled,
                                root.defaultPttOnSoundUrl,
                                "pttOnCuePendingA",
                                requestId)
            } else {
                root.requestCue(pttOnCueB,
                                enabled,
                                root.defaultPttOnSoundUrl,
                                "pttOnCuePendingB",
                                requestId)
            }
            root.pttOnVoiceFlip = !root.pttOnVoiceFlip
        }

        function requestPttOffCue(enabled) {
            root.playAndroidCueTone(2, enabled)
            if (root.pttOffVoiceFlip) {
                root.requestCue(pttOffCueA,
                                enabled,
                                root.defaultPttOffSoundUrl,
                                "pttOffCuePendingA")
            } else {
                root.requestCue(pttOffCueB,
                                enabled,
                                root.defaultPttOffSoundUrl,
                                "pttOffCuePendingB")
            }
            root.pttOffVoiceFlip = !root.pttOffVoiceFlip
        }

        function playCarrierSenseCue() {
            var nowMs = Date.now()
            if (nowMs - root.lastCarrierCueMs < 150)
                return
            root.lastCarrierCueMs = nowMs
            root.playAndroidCueTone(3, root.carrierSenseSoundEnabled)
            root.requestCue(carrierSenseCue,
                            root.carrierSenseSoundEnabled,
                            root.defaultCarrierSenseSoundUrl,
                            "carrierCuePending")
        }

        function playPttOnCue() {
            root.pttOnPlayRequestId = root.pttOnPlayRequestId + 1
            const requestId = root.pttOnPlayRequestId
            root.pttOnCueRecoveryRequestId = 0
            root.requestPttOnCue(root.pttOnSoundEnabled, requestId)
            if (root.pttOnSoundEnabled) {
                root.pttOnRetryAttempts = 8
                pttOnRetryTimer.requestId = requestId
                pttOnRetryTimer.restart()
            }
        }

        Connections {
            target: appState
            function onPttPressedChanged() {
                if (appState.pttPressed) {
                    if (!root.pttOnPlayedForCurrentTx) {
                        root.pttOnPlayedForCurrentTx = true
                        root.playPttOnCue()
                    }
                    return
                }
                if (!appState.pttPressed)
                {
                    root.pttOnPlayedForCurrentTx = false
                    root.pttOnRetryAttempts = 0
                    pttOnRetryTimer.stop()
                }
            }
            function onTalkerIdChanged() {
                if (!appState.pttPressed)
                    return
                if (appState.talkerId === 0)
                    return
                if (appState.talkerId === appState.selfId)
                    return
                root.playCarrierSenseCue()
            }
        }

        Connections {
            target: channelManager
            function onTalkReleasePacketDetected(talkerId) {
                // Intentionally no cue here.
                // ptt_off must be played only after playout drain completes.
            }
            function onTalkReleasePlayoutCompleted(talkerId) {
                if (talkerId === 0)
                    return
                if (talkerId === appState.selfId)
                    return
                root.requestPttOffCue(root.pttOffSoundEnabled)
            }
            function onTalkDenied(currentTalkerId) {
                root.playCarrierSenseCue()
            }
        }

        Connections {
            target: appState
            function onLinkStatusChanged() {
                if (appState.linkStatus.indexOf("Busy:") === 0)
                    root.playCarrierSenseCue()
            }
        }

        Timer {
            id: cueWarmupTimer
            interval: 700
            repeat: false
            onTriggered: {
                if (pttOnCueA.status !== SoundEffect.Ready &&
                    pttOnCueB.status !== SoundEffect.Ready)
                    root.pttOnSoundUrl = root.defaultPttOnSoundUrl
                if (pttOffCueA.status !== SoundEffect.Ready &&
                    pttOffCueB.status !== SoundEffect.Ready)
                    root.pttOffSoundUrl = root.defaultPttOffSoundUrl
                if (carrierSenseCue.status !== SoundEffect.Ready)
                    root.carrierSenseSoundUrl = root.defaultCarrierSenseSoundUrl
            }
        }

        Timer {
            id: pttOnRetryTimer
            property int requestId: 0
            interval: 140
            repeat: true
            onTriggered: {
                if (!root.pttOnSoundEnabled)
                {
                    stop()
                    return
                }
                if (requestId !== root.pttOnPlayRequestId)
                {
                    stop()
                    return
                }
                if (root.pttOnPlayStartedId >= requestId)
                {
                    stop()
                    return
                }
                if (root.pttOnRetryAttempts <= 0)
                {
                    if (root.pttOnCueRecoveryRequestId !== requestId)
                    {
                        root.pttOnCueRecoveryRequestId = requestId
                        root.recoverCueEngine(true)
                        root.pttOnRetryAttempts = 6
                        return
                    }
                    stop()
                    return
                }
                if (root.pttOnRetryAttempts === 4 &&
                    root.pttOnCueRecoveryRequestId !== requestId)
                {
                    root.pttOnCueRecoveryRequestId = requestId
                    root.recoverCueEngine(true)
                }
                root.requestPttOnCue(true, requestId)
                root.pttOnRetryAttempts = root.pttOnRetryAttempts - 1
            }
        }

        MediaDevices {
            id: cueMediaDevices
            onAudioOutputsChanged: {
                root.syncCueAudioDevice()
                root.recoverCueEngine()
            }
        }

        FileDialog {
            id: cueFileDialog
            title: "Select audio file"
            fileMode: FileDialog.OpenFile
            nameFilters: ["Audio files (*.wav *.ogg *.mp3 *.m4a)", "All files (*)"]
            onAccepted: {
                var selected = selectedFile
                if (!selected || selected.toString().length === 0)
                    return

                if (root.cuePickerTarget === "ptt_on")
                    root.pttOnSoundUrl = selected
                else if (root.cuePickerTarget === "ptt_off")
                    root.pttOffSoundUrl = selected
                else if (root.cuePickerTarget === "carrier")
                    root.carrierSenseSoundUrl = selected
                root.cuePickerTarget = ""
            }
            onRejected: root.cuePickerTarget = ""
        }

        FileDialog {
            id: codec2LibFileDialog
            title: "Select codec2 dynamic library"
            fileMode: FileDialog.OpenFile
            nameFilters: ["Dynamic libraries (*.dll *.so *.dylib)", "All files (*)"]
            onAccepted: {
                var selected = selectedFile
                if (!selected || selected.toString().length === 0)
                    return
                appState.codec2LibraryPath = selected.toString()
            }
        }

        FileDialog {
            id: opusLibFileDialog
            title: "Select opus dynamic library"
            fileMode: FileDialog.OpenFile
            nameFilters: ["Dynamic libraries (*.dll *.so *.dylib)", "All files (*)"]
            onAccepted: {
                var selected = selectedFile
                if (!selected || selected.toString().length === 0)
                    return
                appState.opusLibraryPath = selected.toString()
            }
        }

        Menu {
            id: overflowMenu
            x: tabRow.x + menuButton.x + menuButton.width - width
            y: tabRow.y + menuButton.height

            MenuItem {
                text: "Licenses"
                onTriggered: {
                    root.refreshLicensesText()
                    licensesDialog.open()
                }
            }

            MenuItem {
                text: "Download libcodec2.so"
                onTriggered: Qt.openUrlExternally(root.codec2DownloadUrl)
            }
        }

        Dialog {
            id: licensesDialog
            modal: true
            title: "Licenses"
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            width: Math.min(root.width - 20, 640)
            height: Math.min(root.height - 20, 760)
            standardButtons: Dialog.Close

            contentItem: ScrollView {
                clip: true
                TextArea {
                    readOnly: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: root.licensesText
                    color: "#cfd8dc"
                    font.pixelSize: 12
                    background: Rectangle { color: "#0f141a" }
                }
            }
        }

        Column {
            anchors.fill: parent
            anchors.leftMargin: 12 + root.safeInsetLeft
            anchors.rightMargin: 12 + root.safeInsetRight
            anchors.topMargin: 12 + root.safeInsetTop
            anchors.bottomMargin: 12 + root.safeInsetBottom
            spacing: 10

            Row {
                id: tabRow
                width: parent.width
                spacing: 8

                TabBar {
                    id: tabs
                    width: parent.width - menuButton.width - tabRow.spacing
                    onCurrentIndexChanged: persisted.pageIndex = currentIndex

                    TabButton { text: "Page A" }
                    TabButton { text: "Page B" }
                }

                ToolButton {
                    id: menuButton
                    width: 40
                    height: tabs.height
                    text: "\u22EE"
                    font.pixelSize: 20
                    palette.buttonText: "#2b2b2b"
                    background: Rectangle {
                        color: tabs.palette.button
                        border.color: tabs.palette.mid
                        border.width: 1
                        radius: 0
                    }
                    onClicked: overflowMenu.open()
                }
            }

            Loader {
                id: pageLoader
                width: parent.width
                height: parent.height - tabRow.height - 10
                sourceComponent: tabs.currentIndex === 0 ? pageA : pageB
            }
        }

        Component {
            id: pageA

            Item {
                anchors.fill: parent

                function snapSpeakerVolume(value) {
                    var v = Math.round(value)
                    if (Math.abs(v - 100) <= 3)
                        return 100
                    return v
                }

                function snapMicVolume(value) {
                    var v = Math.round(value)
                    if (Math.abs(v - 100) <= 3)
                        return 100
                    return v
                }

                readonly property real pttAreaHeight: Math.round(height / 4)

                Rectangle {
                    id: pttButton
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: pttAreaHeight
                    radius: 12
                    color: !appState.serverOnline ? "#455a64" :
                                                    (appState.pttPressed ? "#e74c3c" : "#2ecc71")
                    border.color: "#263238"
                    border.width: 2

                    Text {
                        anchors.centerIn: parent
                        text: appState.pttPressed ? "PTT ON" : "PTT OFF"
                        color: "#0b0f13"
                        font.pixelSize: 29
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: appState.serverOnline
                        onPressed: appState.pttPressed = true
                        onReleased: appState.pttPressed = false
                        onCanceled: appState.pttPressed = false
                    }
                }

                Text {
                    id: holdToTalkLabel
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: pttButton.top
                    anchors.bottomMargin: 6
                    text: "Hold to talk"
                    color: "#607d8b"
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                }

                Flickable {
                    id: pageAFlick
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: holdToTalkLabel.top
                    anchors.bottomMargin: 8
                    clip: true
                    contentWidth: width
                    contentHeight: pageAContent.height + 8
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar { }

                    Item {
                        id: pageAContent
                        width: pageAFlick.width
                        height: pageAColumn.height

                        Column {
                            id: pageAColumn
                            width: parent.width
                            spacing: 4

                            Text {
                                text: "Codec Bitrate (bps)"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                            Row {
                                spacing: 8

                                Rectangle {
                                    width: 88
                                    height: 28
                                    radius: 6
                                    color: appState.codecSelection === 1 ? "#4db6ac" : "#1a222b"
                                    opacity: root.codec2Selectable ? 1.0 : 0.45
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Codec2"
                                        color: root.codec2Selectable ?
                                                   (appState.codecSelection === 1 ? "#0b0f13" : "#cfd8dc") :
                                                   "#607d8b"
                                        font.pixelSize: 14
                                    }

                                    TapHandler {
                                        enabled: root.codec2Selectable
                                        onTapped: appState.codecSelection = 1
                                    }
                                }

                                Rectangle {
                                    width: 88
                                    height: 28
                                    radius: 6
                                    color: appState.codecSelection === 2 ? "#4db6ac" : "#1a222b"
                                    opacity: root.opusSelectable ? 1.0 : 0.45
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Opus"
                                        color: root.opusSelectable ?
                                                   (appState.codecSelection === 2 ? "#0b0f13" : "#cfd8dc") :
                                                   "#607d8b"
                                        font.pixelSize: 14
                                    }

                                    TapHandler {
                                        enabled: root.opusSelectable
                                        onTapped: appState.codecSelection = 2
                                    }
                                }

                                Rectangle {
                                    width: 88
                                    height: 28
                                    radius: 6
                                    color: appState.codecSelection === 0 ? "#4db6ac" : "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "PCM"
                                        color: appState.codecSelection === 0 ? "#0b0f13" : "#cfd8dc"
                                        font.pixelSize: 14
                                    }

                                    TapHandler { onTapped: appState.codecSelection = 0 }
                                }
                            }

                            ComboBox {
                                id: bitrateCombo
                                width: parent.width
                                model: root.bitrateModelForSelection(appState.codecSelection)
                                enabled: appState.codecSelection !== 0 &&
                                         ((appState.codecSelection === 1 && root.codec2Selectable) ||
                                          (appState.codecSelection === 2 && root.opusSelectable))
                                currentIndex: root.bitrateToIndex(appState.codecBitrate, appState.codecSelection)
                                onActivated: appState.codecBitrate = root.indexToBitrate(currentIndex, appState.codecSelection)

                                contentItem: Text {
                                    leftPadding: 10
                                    rightPadding: 10
                                    text: root.bitrateLabel(parseInt(bitrateCombo.currentText), appState.codecSelection)
                                    color: bitrateCombo.enabled ? "#cfd8dc" : "#607d8b"
                                    font.pixelSize: 15
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                background: Rectangle {
                                    radius: 6
                                    color: "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1
                                }

                                delegate: ItemDelegate {
                                    width: bitrateCombo.width
                                    text: root.bitrateLabel(modelData, appState.codecSelection)
                                    font.pixelSize: 15
                                }
                            }

                            Text {
                                visible: appState.codecSelection === 1 && !root.codec2Selectable
                                text: "Codec2 unavailable: select and load a dynamic library."
                                color: "#78909c"
                                font.pixelSize: 12
                            }

                            Text {
                                visible: appState.codecSelection === 2 && !root.opusSelectable
                                text: "Opus unavailable: place and link libopus prebuilt library."
                                color: "#78909c"
                                font.pixelSize: 12
                            }

                            Text {
                                text: "Mic Volume"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                            Row {
                                width: parent.width
                                spacing: 8

                                Slider {
                                    width: Math.max(120, pageAColumn.width - 52 - 8)
                                    from: 0
                                    to: 200
                                    stepSize: 1
                                    value: appState.micVolumePercent
                                    onMoved: appState.micVolumePercent = snapMicVolume(value)
                                }

                                Rectangle {
                                    width: 52
                                    height: 36
                                    radius: 6
                                    color: "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: appState.micVolumePercent + "%"
                                        color: appState.micVolumePercent === 100 ? "#4db6ac" : "#cfd8dc"
                                        font.pixelSize: 14
                                    }
                                }
                            }

                            Text {
                                text: "Speaker Volume"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                            Row {
                                width: parent.width
                                spacing: 8

                                Slider {
                                    width: Math.max(120, pageAColumn.width - 52 - 8)
                                    from: 0
                                    to: 400
                                    stepSize: 1
                                    value: appState.speakerVolumePercent
                                    onMoved: appState.speakerVolumePercent = snapSpeakerVolume(value)
                                }

                                Rectangle {
                                    width: 52
                                    height: 36
                                    radius: 6
                                    color: "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: appState.speakerVolumePercent + "%"
                                        color: appState.speakerVolumePercent === 100 ? "#4db6ac" : "#cfd8dc"
                                        font.pixelSize: 14
                                    }
                                }
                            }

                            Text {
                                text: "Channel ID"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                            Rectangle {
                                width: parent.width
                                height: 52
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                TextInput {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    color: "#cfd8dc"
                                    text: root.channelId
                                    font.pixelSize: Math.round(parent.height * 0.35)
                                    verticalAlignment: TextInput.AlignVCenter
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onTextChanged: root.channelId = text
                                }
                            }

                            Text {
                                text: "Password"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                            Rectangle {
                                width: parent.width
                                height: 52
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                TextInput {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    color: "#cfd8dc"
                                    text: root.password
                                    font.pixelSize: Math.round(parent.height * 0.35)
                                    verticalAlignment: TextInput.AlignVCenter
                                    echoMode: TextInput.Password
                                    onTextChanged: root.password = text
                                }
                            }

                            Rectangle {
                                id: connectButton
                                width: parent.width
                                height: 52
                                radius: 8
                                property bool inputValid: Number(root.serverPort) > 0 &&
                                                          Number(root.channelId) > 0 &&
                                                          root.serverAddress.length > 0
                                color: inputValid ? "#4db6ac" : "#455a64"
                                opacity: inputValid ? 1.0 : 0.6
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Connect"
                                    color: "#0b0f13"
                                    font.pixelSize: 17
                                }

                                TapHandler {
                                    enabled: connectButton.inputValid
                                    onTapped: channelManager.connectToServer(
                                                  parseInt(root.channelId),
                                                  root.serverAddress,
                                                  parseInt(root.serverPort),
                                                  root.password)
                                }
                            }

                            Text {
                                text: "Channel: " + appState.currentChannelId + "  Self: " + appState.selfId
                                color: "#cfd8dc"
                                font.pixelSize: 17
                            }

                            Text {
                                text: "Status: " + appState.linkStatus
                                color: "#90a4ae"
                                font.pixelSize: 15
                            }

                            Text {
                                text: "Talker: " + appState.talkerId
                                color: "#78909c"
                                font.pixelSize: 15
                            }
                        }
                    }
                }
            }
        }
        Component {
            id: pageB

            Item {
                anchors.fill: parent

                Flickable {
                    anchors.fill: parent
                    contentWidth: Math.max(width, centeredSettings.width)
                    contentHeight: centeredSettings.height + 20
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar { }

                    Item {
                        id: centeredSettings
                        width: parent.width
                        height: contentColumn.height

                        Column {
                            id: contentColumn
                            width: parent.width
                            spacing: 10

                            Text {
                                text: "Server Address"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                        Rectangle {
                            width: parent.width
                            height: 36
                            radius: 6
                            color: "#1a222b"
                            border.color: "#263238"
                            border.width: 1

                            TextInput {
                                anchors.fill: parent
                                anchors.margins: 6
                                color: "#cfd8dc"
                                text: root.serverAddress
                                font.pixelSize: 15
                                verticalAlignment: TextInput.AlignVCenter
                                onTextChanged: root.serverAddress = text
                            }
                        }

                        Text {
                            text: "Server Port"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Rectangle {
                            width: parent.width
                            height: 36
                            radius: 6
                            color: "#1a222b"
                            border.color: "#263238"
                            border.width: 1

                            TextInput {
                                anchors.fill: parent
                                anchors.margins: 6
                                color: "#cfd8dc"
                                text: root.serverPort
                                font.pixelSize: 15
                                verticalAlignment: TextInput.AlignVCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                onTextChanged: root.serverPort = text
                            }
                        }

                        Text {
                            text: "Sender ID"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Rectangle {
                            width: parent.width
                            height: 36
                            radius: 6
                            color: "#1a222b"
                            border.color: "#263238"
                            border.width: 1

                            TextInput {
                                id: senderIdInput
                                anchors.fill: parent
                                anchors.margins: 6
                                color: "#cfd8dc"
                                text: appState.senderId > 0 ? appState.senderId.toString() : ""
                                font.pixelSize: 15
                                verticalAlignment: TextInput.AlignVCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: {
                                    appState.senderId = root.parseSenderIdInput(text)
                                    text = appState.senderId > 0 ? appState.senderId.toString() : ""
                                }
                            }
                        }

                        Text {
                            text: "Microphone Device"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Rectangle {
                            width: parent.width
                            height: 36
                            radius: 6
                            color: "#1a222b"
                            border.color: "#263238"
                            border.width: 1

                            ComboBox {
                                id: micDeviceCombo
                                anchors.fill: parent
                                anchors.margins: 2
                                model: audioInput.inputDeviceNames
                                currentIndex: root.micDeviceIndexFromId(audioInput.selectedInputDeviceId)
                                onActivated: function(index) {
                                    if (index >= 0 && index < audioInput.inputDeviceIds.length)
                                        audioInput.selectedInputDeviceId = audioInput.inputDeviceIds[index]
                                }
                            }
                        }

                        Text {
                            text: "Mic Input Session"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 100
                                height: 28
                                radius: 6
                                color: !appState.keepMicSessionAlwaysOn ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Auto 60s"
                                    color: !appState.keepMicSessionAlwaysOn ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 14
                                }

                                TapHandler { onTapped: appState.keepMicSessionAlwaysOn = false }
                            }

                            Rectangle {
                                width: 120
                                height: 28
                                radius: 6
                                color: appState.keepMicSessionAlwaysOn ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Always On"
                                    color: appState.keepMicSessionAlwaysOn ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 14
                                }

                                TapHandler { onTapped: appState.keepMicSessionAlwaysOn = true }
                            }
                        }

                            Text {
                                text: appState.keepMicSessionAlwaysOn ?
                                          "Warning: Always On increases battery usage." :
                                          "Auto 60s: keeps mic session for 60s after last PTT, then closes."
                                color: "#607d8b"
                                font.pixelSize: 13
                            }

                            Text {
                                text: "Noise Suppressor"
                                color: "#90a4ae"
                                font.pixelSize: 13
                            }

                            Row {
                                spacing: 8

                                Rectangle {
                                    width: 100
                                    height: 28
                                    radius: 6
                                    color: !appState.noiseSuppressionEnabled ? "#4db6ac" : "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Off"
                                        color: !appState.noiseSuppressionEnabled ? "#0b0f13" : "#cfd8dc"
                                        font.pixelSize: 14
                                    }

                                    TapHandler { onTapped: appState.noiseSuppressionEnabled = false }
                                }

                                Rectangle {
                                    width: 100
                                    height: 28
                                    radius: 6
                                    color: appState.noiseSuppressionEnabled ? "#4db6ac" : "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "On"
                                        color: appState.noiseSuppressionEnabled ? "#0b0f13" : "#cfd8dc"
                                        font.pixelSize: 14
                                    }

                                    TapHandler { onTapped: appState.noiseSuppressionEnabled = true }
                                }
                            }

                            Row {
                                width: parent.width
                                spacing: 8

                                Slider {
                                    width: Math.max(120, parent.width - 52 - 8)
                                    from: 0
                                    to: 100
                                    stepSize: 1
                                    enabled: appState.noiseSuppressionEnabled
                                    value: appState.noiseSuppressionLevel
                                    onMoved: appState.noiseSuppressionLevel = Math.round(value)
                                }

                                Rectangle {
                                    width: 52
                                    height: 36
                                    radius: 6
                                    color: "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1
                                    opacity: appState.noiseSuppressionEnabled ? 1.0 : 0.6

                                    Text {
                                        anchors.centerIn: parent
                                        text: appState.noiseSuppressionLevel + "%"
                                        color: "#cfd8dc"
                                        font.pixelSize: 14
                                    }
                                }
                            }

                            Text {
                                text: appState.noiseSuppressionEnabled ?
                                          "Higher values suppress ambient noise more strongly." :
                                          "Noise suppression is disabled."
                                color: "#607d8b"
                                font.pixelSize: 12
                            }

                        Text {
                            text: "TX FEC (RS 2-loss)"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 100
                                height: 28
                                radius: 6
                                color: !appState.fecEnabled ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Off"
                                    color: !appState.fecEnabled ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 14
                                }

                                TapHandler { onTapped: appState.fecEnabled = false }
                            }

                            Rectangle {
                                width: 100
                                height: 28
                                radius: 6
                                color: appState.fecEnabled ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "On"
                                    color: appState.fecEnabled ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 14
                                }

                                TapHandler { onTapped: appState.fecEnabled = true }
                            }
                        }

                        Text {
                            text: "Network QoS (DSCP EF)"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 100
                                height: 28
                                radius: 6
                                color: !appState.qosEnabled ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Off"
                                    color: !appState.qosEnabled ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 14
                                }

                                TapHandler { onTapped: appState.qosEnabled = false }
                            }

                            Rectangle {
                                width: 100
                                height: 28
                                radius: 6
                                color: appState.qosEnabled ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "On"
                                    color: appState.qosEnabled ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 14
                                }

                                TapHandler { onTapped: appState.qosEnabled = true }
                            }
                        }

                        Text {
                            text: appState.qosEnabled ?
                                      "Voice packets request priority with DSCP EF (network may ignore it)." :
                                      "QoS marking is disabled."
                            color: "#607d8b"
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Encryption"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 100
                                height: 36
                                radius: 6
                                color: appState.cryptoMode === 0 ? "#4db6ac" : "#1a222b"
                                opacity: appState.opensslAvailable ? 1.0 : 0.5
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "AES-GCM"
                                    color: appState.cryptoMode === 0 ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 15
                                }

                                TapHandler {
                                    enabled: appState.opensslAvailable
                                    onTapped: appState.cryptoMode = 0
                                }
                            }

                            Rectangle {
                                width: 100
                                height: 36
                                radius: 6
                                color: appState.cryptoMode === 1 ? "#4db6ac" : "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Legacy"
                                    color: appState.cryptoMode === 1 ? "#0b0f13" : "#cfd8dc"
                                    font.pixelSize: 15
                                }

                                TapHandler { onTapped: appState.cryptoMode = 1 }
                            }
                        }

                        Text {
                            text: appState.opensslAvailable ?
                                      "OpenSSL available" :
                                      "OpenSSL not available (Legacy only)"
                            color: "#607d8b"
                            font.pixelSize: 13
                        }

                        Text {
                            text: "Codec2 Dynamic Library"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Pick"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: codec2LibFileDialog.open() }
                            }

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Clear"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: appState.codec2LibraryPath = "" }
                            }
                        }

                        Text {
                            text: appState.codec2LibraryPath.length > 0 ?
                                      root.fileNameFromUrl(appState.codec2LibraryPath) :
                                      "(auto search)"
                            color: "#607d8b"
                            font.pixelSize: 12
                        }

                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: appState.codec2LibraryLoaded ?
                                      "Codec2 library loaded" :
                                      (appState.codec2LibraryError.length > 0 ?
                                           appState.codec2LibraryError :
                                           "Codec2 library not loaded (PCM fallback)")
                            color: appState.codec2LibraryLoaded ? "#4db6ac" : "#78909c"
                            font.pixelSize: 12
                        }

                        Text {
                            text: "Opus Dynamic Library (Optional)"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Pick"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: opusLibFileDialog.open() }
                            }

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Clear"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: appState.opusLibraryPath = "" }
                            }
                        }

                        Text {
                            text: appState.opusLibraryPath.length > 0 ?
                                      root.fileNameFromUrl(appState.opusLibraryPath) :
                                      "(using linked opus)"
                            color: "#607d8b"
                            font.pixelSize: 12
                        }

                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: appState.opusLibraryPath.length === 0 ?
                                      "Using linked Opus library." :
                                      (appState.opusLibraryLoaded ?
                                           "Loaded user-specified Opus library." :
                                           (appState.opusLibraryError.length > 0 ?
                                                appState.opusLibraryError :
                                                "Opus library not loaded (linked fallback)."))
                            color: (appState.opusLibraryPath.length === 0 || appState.opusLibraryLoaded) ?
                                       "#4db6ac" : "#78909c"
                            font.pixelSize: 12
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: "#263238"
                            opacity: 0.6
                        }

                        Text {
                            text: "Cue Sounds"
                            color: "#90a4ae"
                            font.pixelSize: 13
                        }

                        Text {
                            text: "Cue Volume"
                            color: "#cfd8dc"
                            font.pixelSize: 14
                        }

                        Row {
                            width: parent.width
                            spacing: 8

                            Slider {
                                width: Math.max(120, parent.width - 52 - 8)
                                from: 0
                                to: 100
                                value: root.cueVolumePercent
                                onValueChanged: root.cueVolumePercent = Math.round(value)
                            }

                            Rectangle {
                                width: 52
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: root.cueVolumePercent + "%"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                            }
                        }

                        Row {
                            spacing: 8

                            Text {
                                width: 96
                                text: "PTT ON"
                                color: "#cfd8dc"
                                font.pixelSize: 14
                            }

                            Switch {
                                checked: root.pttOnSoundEnabled
                                onToggled: root.pttOnSoundEnabled = checked
                            }

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Pick"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler {
                                    onTapped: {
                                        root.cuePickerTarget = "ptt_on"
                                        cueFileDialog.open()
                                    }
                                }
                            }

                            Rectangle {
                                width: 64
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Default"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: root.pttOnSoundUrl = root.defaultPttOnSoundUrl }
                            }

                            Rectangle {
                                width: 42
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "▶"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler {
                                    onTapped: root.requestPttOnCue(true)
                                }
                            }
                        }

                        Text {
                            text: root.fileNameFromUrl(root.pttOnSoundUrl)
                            color: "#607d8b"
                            font.pixelSize: 12
                        }

                        Row {
                            spacing: 8

                            Text {
                                width: 96
                                text: "PTT OFF"
                                color: "#cfd8dc"
                                font.pixelSize: 14
                            }

                            Switch {
                                checked: root.pttOffSoundEnabled
                                onToggled: root.pttOffSoundEnabled = checked
                            }

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Pick"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler {
                                    onTapped: {
                                        root.cuePickerTarget = "ptt_off"
                                        cueFileDialog.open()
                                    }
                                }
                            }

                            Rectangle {
                                width: 64
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Default"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: root.pttOffSoundUrl = root.defaultPttOffSoundUrl }
                            }

                            Rectangle {
                                width: 42
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "▶"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler {
                                    onTapped: root.requestPttOffCue(true)
                                }
                            }
                        }

                        Text {
                            text: root.fileNameFromUrl(root.pttOffSoundUrl)
                            color: "#607d8b"
                            font.pixelSize: 12
                        }

                        Row {
                            spacing: 8

                            Text {
                                width: 96
                                text: "Carrier Sense"
                                color: "#cfd8dc"
                                font.pixelSize: 14
                            }

                            Switch {
                                checked: root.carrierSenseSoundEnabled
                                onToggled: root.carrierSenseSoundEnabled = checked
                            }

                            Rectangle {
                                width: 56
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Pick"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler {
                                    onTapped: {
                                        root.cuePickerTarget = "carrier"
                                        cueFileDialog.open()
                                    }
                                }
                            }

                            Rectangle {
                                width: 64
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "Default"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler { onTapped: root.carrierSenseSoundUrl = root.defaultCarrierSenseSoundUrl }
                            }

                            Rectangle {
                                width: 42
                                height: 28
                                radius: 6
                                color: "#1a222b"
                                border.color: "#263238"
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "▶"
                                    color: "#cfd8dc"
                                    font.pixelSize: 13
                                }
                                TapHandler {
                                    onTapped: root.requestCue(carrierSenseCue,
                                                              true,
                                                              root.defaultCarrierSenseSoundUrl,
                                                              "carrierCuePending")
                                }
                            }
                        }

                        Text {
                            text: root.fileNameFromUrl(root.carrierSenseSoundUrl)
                            color: "#607d8b"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }
}
}
