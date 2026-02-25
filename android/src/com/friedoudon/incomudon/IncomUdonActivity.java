package com.friedoudon.incomudon;

import android.content.ContentResolver;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.content.Intent;
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
    public static native void onHeadsetPttChanged(boolean pressed);
    private static IncomUdonActivity sInstance;
    private AudioManager mAudioManager;
    private boolean mPttRouteEnabled = false;
    private boolean mPreferCommunicationMode = false;
    private ToneGenerator mToneVoiceCall;
    private ToneGenerator mToneMedia;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;
        mAudioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        forceMediaVolumeStream();
    }

    @Override
    public void onDestroy() {
        stopKeepAliveService();
        mPreferCommunicationMode = false;
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
        forceMediaVolumeStream();
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

    private void applyPreferCommunicationMode(boolean enabled) {
        if (mPreferCommunicationMode == enabled) {
            return;
        }
        mPreferCommunicationMode = enabled;
        applyPttAudioRoute(mPttRouteEnabled, true);
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
            final boolean useCommunicationMode = mPreferCommunicationMode;

            if (useCommunicationMode) {
                mAudioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    try {
                        final AudioDeviceInfo comm = findBluetoothCommunicationDevice();
                        if (comm != null) {
                            mAudioManager.setCommunicationDevice(comm);
                        }
                    } catch (Exception ignored) {
                    }
                }
                try {
                    mAudioManager.setSpeakerphoneOn(false);
                } catch (Exception ignored) {
                }
            } else {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    try {
                        mAudioManager.clearCommunicationDevice();
                    } catch (Exception ignored) {
                    }
                }
                mAudioManager.setMode(AudioManager.MODE_NORMAL);
                try {
                    mAudioManager.setSpeakerphoneOn(false);
                } catch (Exception ignored) {
                }
            }
            if (enabled) {
                mAudioManager.setMicrophoneMute(false);
            }
            forceMediaVolumeStream();
        } catch (SecurityException ignored) {
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
        final int stream = mPreferCommunicationMode
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
            setVolumeControlStream(AudioManager.STREAM_MUSIC);
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
