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
        property var bitrateOptions: [450, 700, 1600, 2400, 3200]
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

        function normalizeBitrate(value) {
            var best = bitrateOptions[0]
            var bestDiff = Math.abs(value - best)
            for (var i = 1; i < bitrateOptions.length; ++i) {
                var diff = Math.abs(value - bitrateOptions[i])
                if (diff < bestDiff) {
                    bestDiff = diff
                    best = bitrateOptions[i]
                }
            }
            return best
        }

        function bitrateToIndex(bps) {
            for (var i = 0; i < bitrateOptions.length; ++i) {
                if (bitrateOptions[i] === bps)
                    return i
            }
            return 0
        }

        function indexToBitrate(idx) {
            if (idx < 0)
                return bitrateOptions[0]
            if (idx >= bitrateOptions.length)
                return bitrateOptions[bitrateOptions.length - 1]
            return bitrateOptions[idx]
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
            if (!codec2Selectable && !appState.forcePcm) {
                root.suppressForcePcmPersistence = true
                appState.forcePcm = true
                root.suppressForcePcmPersistence = false
                return
            }
            if (codec2Selectable && appState.forcePcm && !persisted.forcePcm) {
                root.suppressForcePcmPersistence = true
                appState.forcePcm = false
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
            property string channelId: "100"
            property string password: ""
            property int codecBitrate: 1600
            property bool forcePcm: true
            property bool txFecEnabled: true
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
            property string micInputDeviceId: ""
        }

        Component.onCompleted: {
            root.serverAddress = persisted.serverAddress
            root.serverPort = persisted.serverPort
            root.channelId = persisted.channelId
            root.password = persisted.password

            appState.codecBitrate = root.normalizeBitrate(persisted.codecBitrate)
            appState.forcePcm = persisted.forcePcm
            appState.fecEnabled = persisted.txFecEnabled
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
            audioInput.selectedInputDeviceId = persisted.micInputDeviceId

            root.syncCodec2Availability()
            root.syncCueAudioDevice()

            tabs.currentIndex = Math.max(0, Math.min(1, persisted.pageIndex))
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
            function onCodecBitrateChanged() {
                persisted.codecBitrate = appState.codecBitrate
            }
            function onForcePcmChanged() {
                if (!root.suppressForcePcmPersistence)
                    persisted.forcePcm = appState.forcePcm
            }
            function onFecEnabledChanged() { persisted.txFecEnabled = appState.fecEnabled }
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

        Column {
            anchors.fill: parent
            anchors.leftMargin: 12 + root.safeInsetLeft
            anchors.rightMargin: 12 + root.safeInsetRight
            anchors.topMargin: 12 + root.safeInsetTop
            anchors.bottomMargin: 12 + root.safeInsetBottom
            spacing: 10

            TabBar {
                id: tabs
                width: parent.width
                onCurrentIndexChanged: persisted.pageIndex = currentIndex

                TabButton { text: "Page A" }
                TabButton { text: "Page B" }
            }

            Loader {
                id: pageLoader
                width: parent.width
                height: parent.height - tabs.height - 10
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
                                    width: 100
                                    height: 28
                                    radius: 6
                                    color: (!appState.forcePcm && root.codec2Selectable) ? "#4db6ac" : "#1a222b"
                                    opacity: root.codec2Selectable ? 1.0 : 0.45
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Codec2"
                                        color: root.codec2Selectable ?
                                                   (!appState.forcePcm ? "#0b0f13" : "#cfd8dc") :
                                                   "#607d8b"
                                        font.pixelSize: 14
                                    }

                                    TapHandler {
                                        enabled: root.codec2Selectable
                                        onTapped: appState.forcePcm = false
                                    }
                                }

                                Rectangle {
                                    width: 100
                                    height: 28
                                    radius: 6
                                    color: appState.forcePcm ? "#4db6ac" : "#1a222b"
                                    border.color: "#263238"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "PCM"
                                        color: appState.forcePcm ? "#0b0f13" : "#cfd8dc"
                                        font.pixelSize: 14
                                    }

                                    TapHandler { onTapped: appState.forcePcm = true }
                                }
                            }

                            ComboBox {
                                id: bitrateCombo
                                width: parent.width
                                model: root.bitrateOptions
                                enabled: !appState.forcePcm && root.codec2Selectable
                                currentIndex: root.bitrateToIndex(appState.codecBitrate)
                                onActivated: appState.codecBitrate = root.indexToBitrate(currentIndex)

                                contentItem: Text {
                                    leftPadding: 10
                                    rightPadding: 10
                                    text: bitrateCombo.currentText + " bps"
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
                                    text: modelData + " bps"
                                    font.pixelSize: 15
                                }
                            }

                            Text {
                                visible: !root.codec2Selectable
                                text: "Codec2 unavailable: select and load a dynamic library."
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
