package com.friedoudon.incomudon;

import android.content.ContentResolver;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.KeyEvent;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Locale;
import org.qtproject.qt.android.bindings.QtActivity;

public class IncomUdonActivity extends QtActivity {
    private static final int OUTPUT_ROUTE_AUTO = 0;
    private static final int OUTPUT_ROUTE_SPEAKER = 1;
    private static final int OUTPUT_ROUTE_BLUETOOTH = 2;
    private static final int OUTPUT_ROUTE_USB = 3;
    private static final int OUTPUT_ROUTE_WIRED = 4;

    public static native void onHeadsetPttChanged(boolean pressed);
    public static native void onNetworkAvailabilityChanged(boolean available);
    public static native void onAudioRouteChanged();
    private static IncomUdonActivity sInstance;
    private AudioManager mAudioManager;
    private ConnectivityManager mConnectivityManager;
    private ConnectivityManager.NetworkCallback mNetworkCallback;
    private boolean mNetworkCallbackRegistered = false;
    private boolean mLastNetworkAvailable = true;
    private boolean mPttRouteEnabled = false;
    private boolean mPreferCommunicationMode = false;
    private boolean mCommunicationRouteActive = false;
    private int mPreferredOutputRoute = OUTPUT_ROUTE_AUTO;
    private ToneGenerator mToneVoiceCall;
    private ToneGenerator mToneMedia;
    private AudioDeviceCallback mAudioDeviceCallback;
    private final Runnable mRouteRefreshRunnable = new Runnable() {
        @Override
        public void run() {
            applyPttAudioRoute(mPttRouteEnabled, true);
            forceMediaVolumeStream();
            notifyAudioRouteChanged();
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;
        mAudioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        mConnectivityManager = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
        registerAudioDeviceCallback();
        applyPttAudioRoute(false, true);
        forceMediaVolumeStream();
        scheduleRouteRefresh(250);
        registerNetworkCallback();
        notifyNetworkAvailabilityChanged(isNetworkAvailable());
    }

    @Override
    public void onDestroy() {
        unregisterNetworkCallback();
        unregisterAudioDeviceCallback();
        stopKeepAliveService();
        mPreferCommunicationMode = false;
        mPreferredOutputRoute = OUTPUT_ROUTE_AUTO;
        applyPttAudioRoute(false, true);
        releaseToneGenerators();
        if (sInstance == this) {
            sInstance = null;
        }
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyPttAudioRoute(mPttRouteEnabled, true);
        forceMediaVolumeStream();
        scheduleRouteRefresh(150);
    }

    public static void setPttAudioRoute(final boolean enabled) {
        final IncomUdonActivity activity = sInstance;
        if (activity == null) {
            return;
        }

        activity.runOnUiThread(() -> activity.applyPttAudioRoute(enabled, false));
    }

    public static void setPreferCommunicationMode(final boolean enabled) {
        final IncomUdonActivity activity = sInstance;
        if (activity == null) {
            return;
        }

        activity.runOnUiThread(() -> activity.applyPreferCommunicationMode(enabled));
    }

    public static void setPreferredOutputRoute(final int route) {
        final IncomUdonActivity activity = sInstance;
        if (activity == null) {
            return;
        }

        activity.runOnUiThread(() -> activity.applyPreferredOutputRoute(route));
    }

    public static void playCueTone(final int cueId) {
        final IncomUdonActivity activity = sInstance;
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(() -> activity.playCueToneInternal(cueId));
    }

    public static void setKeepAliveServiceEnabled(final boolean enabled) {
        final IncomUdonActivity activity = sInstance;
        if (activity == null) {
            return;
        }

        activity.runOnUiThread(() -> {
            if (enabled) {
                activity.startKeepAliveService();
            } else {
                activity.stopKeepAliveService();
            }
        });
    }

    public static String copyContentUriToLocalLib(final String uriText) {
        final IncomUdonActivity activity = sInstance;
        if (activity == null || uriText == null || uriText.isEmpty()) {
            return "";
        }

        final Uri uri = Uri.parse(uriText);
        final String scheme = uri.getScheme();
        if (scheme == null || !scheme.toLowerCase(Locale.ROOT).equals("content")) {
            return uriText;
        }

        try {
            activity.getContentResolver().takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
            // Some providers do not grant persistable permission; continue.
        }

        final File dstDir = new File(activity.getFilesDir(), "codec2_libs");
        if (!dstDir.exists() && !dstDir.mkdirs()) {
            return "";
        }

        String displayName = queryDisplayName(activity.getContentResolver(), uri);
        if (displayName == null || displayName.isEmpty()) {
            displayName = "libcodec2_user.so";
        }
        if (!displayName.toLowerCase(Locale.ROOT).endsWith(".so")) {
            displayName = displayName + ".so";
        }
        displayName = displayName.replaceAll("[^A-Za-z0-9._-]", "_");
        final File dst = new File(dstDir, "selected_" + displayName);

        try (InputStream in = activity.getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(dst, false)) {
            if (in == null) {
                return "";
            }
            final byte[] buf = new byte[8192];
            int read;
            while ((read = in.read(buf)) > 0) {
                out.write(buf, 0, read);
            }
            out.flush();
            dst.setReadable(true, true);
            return dst.getAbsolutePath();
        } catch (Exception e) {
            return "";
        }
    }

    private static String queryDisplayName(ContentResolver resolver, Uri uri) {
        try (android.database.Cursor cursor =
                 resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME},
                                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                final int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    return cursor.getString(index);
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private void registerNetworkCallback() {
        if (mConnectivityManager == null || mNetworkCallbackRegistered) {
            return;
        }

        mNetworkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                // Force notify to detect interface switches even when availability
                // stays true (e.g. Wi-Fi <-> mobile handover).
                notifyNetworkAvailabilityChanged(true, true);
            }

            @Override
            public void onLost(Network network) {
                notifyNetworkAvailabilityChanged(isNetworkAvailable(), false);
            }

            @Override
            public void onUnavailable() {
                notifyNetworkAvailabilityChanged(false, false);
            }
        };

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                mConnectivityManager.registerDefaultNetworkCallback(mNetworkCallback);
            } else {
                NetworkRequest request = new NetworkRequest.Builder()
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .build();
                mConnectivityManager.registerNetworkCallback(request, mNetworkCallback);
            }
            mNetworkCallbackRegistered = true;
        } catch (Exception ignored) {
            mNetworkCallback = null;
            mNetworkCallbackRegistered = false;
        }
    }

    private void registerAudioDeviceCallback() {
        if (mAudioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M ||
            mAudioDeviceCallback != null) {
            return;
        }

        mAudioDeviceCallback = new AudioDeviceCallback() {
            @Override
            public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
                scheduleRouteRefresh(80);
            }

            @Override
            public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
                scheduleRouteRefresh(80);
            }
        };

        try {
            mAudioManager.registerAudioDeviceCallback(mAudioDeviceCallback, null);
        } catch (Exception ignored) {
            mAudioDeviceCallback = null;
        }
    }

    private void unregisterAudioDeviceCallback() {
        if (mAudioManager == null || mAudioDeviceCallback == null) {
            return;
        }
        try {
            mAudioManager.unregisterAudioDeviceCallback(mAudioDeviceCallback);
        } catch (Exception ignored) {
        }
        mAudioDeviceCallback = null;
    }

    private void scheduleRouteRefresh(long delayMs) {
        runOnUiThread(() -> {
            if (getWindow() == null || getWindow().getDecorView() == null) {
                applyPttAudioRoute(mPttRouteEnabled, true);
                forceMediaVolumeStream();
                return;
            }
            getWindow().getDecorView().removeCallbacks(mRouteRefreshRunnable);
            getWindow().getDecorView().postDelayed(mRouteRefreshRunnable, Math.max(0L, delayMs));
        });
    }

    private void unregisterNetworkCallback() {
        if (mConnectivityManager == null || !mNetworkCallbackRegistered || mNetworkCallback == null) {
            return;
        }
        try {
            mConnectivityManager.unregisterNetworkCallback(mNetworkCallback);
        } catch (Exception ignored) {
        }
        mNetworkCallback = null;
        mNetworkCallbackRegistered = false;
    }

    private boolean isNetworkAvailable() {
        if (mConnectivityManager == null) {
            return false;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                Network network = mConnectivityManager.getActiveNetwork();
                if (network == null) {
                    return false;
                }
                NetworkCapabilities caps = mConnectivityManager.getNetworkCapabilities(network);
                return caps != null && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
            } catch (Exception ignored) {
                return false;
            }
        }

        try {
            android.net.NetworkInfo info = mConnectivityManager.getActiveNetworkInfo();
            return info != null && info.isConnected();
        } catch (Exception ignored) {
            return false;
        }
    }

    private void notifyNetworkAvailabilityChanged(boolean available) {
        notifyNetworkAvailabilityChanged(available, false);
    }

    private void notifyNetworkAvailabilityChanged(boolean available, boolean force) {
        if (!force && mLastNetworkAvailable == available) {
            return;
        }
        mLastNetworkAvailable = available;
        try {
            onNetworkAvailabilityChanged(available);
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private void notifyAudioRouteChanged() {
        try {
            onAudioRouteChanged();
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    private void applyPreferCommunicationMode(boolean enabled) {
        if (mPreferCommunicationMode == enabled) {
            return;
        }
        mPreferCommunicationMode = enabled;
        applyPttAudioRoute(mPttRouteEnabled, true);
    }

    private void applyPreferredOutputRoute(int route) {
        int normalized = route;
        if (normalized < OUTPUT_ROUTE_AUTO || normalized > OUTPUT_ROUTE_WIRED) {
            normalized = OUTPUT_ROUTE_AUTO;
        }
        if (mPreferredOutputRoute == normalized) {
            return;
        }
        mPreferredOutputRoute = normalized;
        applyPttAudioRoute(mPttRouteEnabled, true);
    }

    private boolean shouldUseCommunicationMode() {
        final boolean bluetoothAvailable = hasBluetoothRouteActive();
        if (!bluetoothAvailable) {
            return false;
        }
        if (mPreferCommunicationMode) {
            return true;
        }
        if (mPreferredOutputRoute == OUTPUT_ROUTE_BLUETOOTH) {
            return true;
        }
        if (mPreferredOutputRoute == OUTPUT_ROUTE_AUTO) {
            return true;
        }
        return false;
    }

    private boolean shouldUseBluetoothSco() {
        if (!hasBluetoothRouteActive()) {
            return false;
        }
        if (mPreferredOutputRoute == OUTPUT_ROUTE_BLUETOOTH) {
            return true;
        }
        if (mPreferredOutputRoute == OUTPUT_ROUTE_AUTO) {
            return true;
        }
        return mPreferCommunicationMode;
    }

    private void applySpeakerphonePreference() {
        if (mAudioManager == null) {
            return;
        }

        try {
            if (mPreferredOutputRoute == OUTPUT_ROUTE_SPEAKER) {
                mAudioManager.setSpeakerphoneOn(true);
            } else if (mPreferredOutputRoute != OUTPUT_ROUTE_AUTO) {
                mAudioManager.setSpeakerphoneOn(false);
            }
        } catch (Exception ignored) {
        }
    }

    private void applyPttAudioRoute(boolean enabled, boolean forceApply) {
        if (mAudioManager == null) {
            return;
        }
        if (!forceApply && mPttRouteEnabled == enabled) {
            return;
        }
        mPttRouteEnabled = enabled;

        try {
            final boolean useCommunicationMode = shouldUseCommunicationMode();
            final boolean useBluetoothSco = useCommunicationMode && shouldUseBluetoothSco();

            if (useCommunicationMode) {
                if (!mCommunicationRouteActive || forceApply) {
                    mAudioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
                    try {
                        if (useBluetoothSco) {
                            mAudioManager.startBluetoothSco();
                            mAudioManager.setBluetoothScoOn(true);
                        } else {
                            mAudioManager.setBluetoothScoOn(false);
                        }
                    } catch (Exception ignored) {
                    }
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        try {
                            final AudioDeviceInfo comm = findPreferredCommunicationDevice();
                            if (comm != null) {
                                mAudioManager.setCommunicationDevice(comm);
                            }
                        } catch (Exception ignored) {
                        }
                    }
                    mCommunicationRouteActive = true;
                }
                applySpeakerphonePreference();
            } else {
                if (mCommunicationRouteActive) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        try {
                            mAudioManager.clearCommunicationDevice();
                        } catch (Exception ignored) {
                        }
                    }
                    try {
                        mAudioManager.stopBluetoothSco();
                    } catch (Exception ignored) {
                    }
                    try {
                        mAudioManager.setBluetoothScoOn(false);
                    } catch (Exception ignored) {
                    }
                    mAudioManager.setMode(AudioManager.MODE_NORMAL);
                    mCommunicationRouteActive = false;
                }
                applySpeakerphonePreference();
            }
            if (enabled) {
                mAudioManager.setMicrophoneMute(false);
            }
            forceMediaVolumeStream();
        } catch (SecurityException ignored) {
        }
    }

    private AudioDeviceInfo findPreferredCommunicationDevice() {
        if (mAudioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return null;
        }
        try {
            for (AudioDeviceInfo d : mAudioManager.getAvailableCommunicationDevices()) {
                if (matchesPreferredRoute(d)) {
                    return d;
                }
            }
            if (mPreferredOutputRoute == OUTPUT_ROUTE_AUTO || mPreferredOutputRoute == OUTPUT_ROUTE_BLUETOOTH) {
                final AudioDeviceInfo bt = findBluetoothCommunicationDevice();
                if (bt != null) {
                    return bt;
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private boolean matchesPreferredRoute(AudioDeviceInfo d) {
        if (d == null) {
            return false;
        }
        final int type = d.getType();
        switch (mPreferredOutputRoute) {
            case OUTPUT_ROUTE_SPEAKER:
                return type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER;
            case OUTPUT_ROUTE_BLUETOOTH:
                return isBluetoothCommunicationDevice(d);
            case OUTPUT_ROUTE_USB:
                return isUsbDevice(d);
            case OUTPUT_ROUTE_WIRED:
                return isWiredDevice(d);
            case OUTPUT_ROUTE_AUTO:
            default:
                return false;
        }
    }

    private AudioDeviceInfo findBluetoothCommunicationDevice() {
        if (mAudioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return null;
        }
        try {
            for (AudioDeviceInfo d : mAudioManager.getAvailableCommunicationDevices()) {
                if (isBluetoothCommunicationDevice(d)) {
                    return d;
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private boolean hasBluetoothRouteActive() {
        if (mAudioManager == null) {
            return false;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                final AudioDeviceInfo comm = mAudioManager.getCommunicationDevice();
                if (isBluetoothDevice(comm)) {
                    return true;
                }
            } catch (Exception ignored) {
            }
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                for (AudioDeviceInfo d : mAudioManager.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
                    if (isBluetoothDevice(d)) {
                        return true;
                    }
                }
                for (AudioDeviceInfo d : mAudioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                    if (isBluetoothDevice(d)) {
                        return true;
                    }
                }
            } catch (Exception ignored) {
            }
        }

        try {
            if (mAudioManager.isBluetoothScoOn() || mAudioManager.isBluetoothA2dpOn()) {
                return true;
            }
        } catch (Exception ignored) {
        }

        return false;
    }

    private static boolean isBluetoothDevice(AudioDeviceInfo d) {
        if (d == null) {
            return false;
        }
        final int type = d.getType();
        if (type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
            type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
            return true;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (type == AudioDeviceInfo.TYPE_BLE_HEADSET ||
                type == AudioDeviceInfo.TYPE_BLE_SPEAKER) {
                return true;
            }
        }
        return false;
    }

    private static boolean isBluetoothCommunicationDevice(AudioDeviceInfo d) {
        if (d == null) {
            return false;
        }
        final int type = d.getType();
        if (type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO) {
            return true;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (type == AudioDeviceInfo.TYPE_BLE_HEADSET) {
                return true;
            }
        }
        return false;
    }

    private static boolean isUsbDevice(AudioDeviceInfo d) {
        if (d == null) {
            return false;
        }
        final int type = d.getType();
        if (type == AudioDeviceInfo.TYPE_USB_DEVICE ||
            type == AudioDeviceInfo.TYPE_USB_ACCESSORY) {
            return true;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (type == AudioDeviceInfo.TYPE_USB_HEADSET) {
                return true;
            }
        }
        return false;
    }

    private static boolean isWiredDevice(AudioDeviceInfo d) {
        if (d == null) {
            return false;
        }
        final int type = d.getType();
        return type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
               type == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
               type == AudioDeviceInfo.TYPE_LINE_ANALOG ||
               type == AudioDeviceInfo.TYPE_LINE_DIGITAL;
    }

    private void playCueToneInternal(int cueId) {
        final ToneGenerator tg = obtainCueToneGenerator();
        if (tg == null) {
            return;
        }

        int toneType = ToneGenerator.TONE_PROP_ACK;
        int durationMs = 90;
        switch (cueId) {
            case 1: // PTT ON
                toneType = ToneGenerator.TONE_PROP_BEEP2;
                durationMs = 85;
                break;
            case 2: // PTT OFF
                toneType = ToneGenerator.TONE_PROP_BEEP;
                durationMs = 70;
                break;
            case 3: // Carrier/BUSY
                toneType = ToneGenerator.TONE_PROP_NACK;
                durationMs = 110;
                break;
            default:
                break;
        }

        try {
            tg.startTone(toneType, durationMs);
        } catch (Exception ignored) {
        }
    }

    private ToneGenerator obtainCueToneGenerator() {
        final int stream = shouldUseCommunicationMode()
            ? AudioManager.STREAM_VOICE_CALL
            : AudioManager.STREAM_MUSIC;
        if (stream == AudioManager.STREAM_VOICE_CALL) {
            if (mToneVoiceCall == null) {
                try {
                    mToneVoiceCall = new ToneGenerator(AudioManager.STREAM_VOICE_CALL, 90);
                } catch (Exception ignored) {
                    mToneVoiceCall = null;
                }
            }
            return mToneVoiceCall;
        }

        if (mToneMedia == null) {
            try {
                mToneMedia = new ToneGenerator(AudioManager.STREAM_MUSIC, 90);
            } catch (Exception ignored) {
                mToneMedia = null;
            }
        }
        return mToneMedia;
    }

    private void releaseToneGenerators() {
        if (mToneVoiceCall != null) {
            try {
                mToneVoiceCall.release();
            } catch (Exception ignored) {
            }
            mToneVoiceCall = null;
        }
        if (mToneMedia != null) {
            try {
                mToneMedia.release();
            } catch (Exception ignored) {
            }
            mToneMedia = null;
        }
    }

    private void forceMediaVolumeStream() {
        try {
            final boolean useVoiceStream = shouldUseCommunicationMode();
            setVolumeControlStream(useVoiceStream
                ? AudioManager.STREAM_VOICE_CALL
                : AudioManager.STREAM_MUSIC);
        } catch (Exception ignored) {
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        final int keyCode = event.getKeyCode();
        if (keyCode == KeyEvent.KEYCODE_HEADSETHOOK ||
            keyCode == KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE) {
            final int action = event.getAction();
            if (action == KeyEvent.ACTION_UP) {
                // Single-button wired remotes share the mic line; while physically pressed
                // the mic signal can be attenuated. Toggle PTT on release instead of hold.
                onHeadsetPttChanged(true);
                return true;
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private void startKeepAliveService() {
        Intent intent = new Intent(this, IncomUdonKeepAliveService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    private void stopKeepAliveService() {
        Intent intent = new Intent(this, IncomUdonKeepAliveService.class);
        stopService(intent);
    }
}
