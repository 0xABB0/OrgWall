package orgwall.melody.audiopolicy;

import android.app.Application;
import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.lang.reflect.Method;

public final class MelodyAudioPolicy {
    private static AudioFocusRequest focusRequest;
    private static AudioManager.OnAudioFocusChangeListener listener;
    private static boolean communicationDeviceSet;

    private MelodyAudioPolicy() {}

    private static native void nativeFocusChange(int change);

    private static Application application() {
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Method cur = at.getMethod("currentApplication");
            Object app = cur.invoke(null);
            return app instanceof Application ? (Application) app : null;
        } catch (Throwable t) {
            return null;
        }
    }

    private static AudioManager audioManager() {
        Application app = application();
        if (app == null) return null;
        return (AudioManager) app.getSystemService(Context.AUDIO_SERVICE);
    }

    public static int requestFocus(int gainType, int usage, int contentType, boolean pauseWhenDucked) {
        AudioManager am = audioManager();
        if (am == null) return AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        AudioFocusRequest previous = focusRequest;
        focusRequest = null;
        if (previous != null) am.abandonAudioFocusRequest(previous);
        if (listener == null) {
            listener = new AudioManager.OnAudioFocusChangeListener() {
                @Override public void onAudioFocusChange(int change) { nativeFocusChange(change); }
            };
        }
        AudioAttributes attrs = new AudioAttributes.Builder()
            .setUsage(usage)
            .setContentType(contentType)
            .build();
        AudioFocusRequest req = new AudioFocusRequest.Builder(gainType)
            .setAudioAttributes(attrs)
            .setWillPauseWhenDucked(pauseWhenDucked)
            .setOnAudioFocusChangeListener(listener, new Handler(Looper.getMainLooper()))
            .build();
        int result = am.requestAudioFocus(req);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) focusRequest = req;
        return result;
    }

    public static void abandonFocus() {
        AudioFocusRequest req = focusRequest;
        focusRequest = null;
        if (req == null) return;
        AudioManager am = audioManager();
        if (am != null) am.abandonAudioFocusRequest(req);
    }

    public static boolean setMode(int mode) {
        AudioManager am = audioManager();
        if (am == null) return false;
        am.setMode(mode);
        return am.getMode() == mode;
    }

    public static boolean setSpeakerCommunicationDevice() {
        if (Build.VERSION.SDK_INT < 31) return false;
        AudioManager am = audioManager();
        if (am == null) return false;
        for (AudioDeviceInfo dev : am.getAvailableCommunicationDevices()) {
            if (dev.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
                boolean ok = am.setCommunicationDevice(dev);
                if (ok) communicationDeviceSet = true;
                return ok;
            }
        }
        return false;
    }

    public static void clearCommunicationDevice() {
        if (Build.VERSION.SDK_INT < 31) return;
        if (!communicationDeviceSet) return;
        communicationDeviceSet = false;
        AudioManager am = audioManager();
        if (am != null) am.clearCommunicationDevice();
    }
}
