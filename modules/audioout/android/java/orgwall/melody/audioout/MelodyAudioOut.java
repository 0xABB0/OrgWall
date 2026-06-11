package orgwall.melody.audioout;

import android.app.Application;
import android.content.Context;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;

import java.lang.reflect.Method;

public final class MelodyAudioOut {
    private static AudioDeviceCallback deviceCallback;

    private MelodyAudioOut() {}

    private static native void nativeDevicesChanged();

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

    public static boolean startDeviceListening() {
        if (deviceCallback != null) return true;
        Application app = application();
        if (app == null) return false;
        AudioManager am = (AudioManager) app.getSystemService(Context.AUDIO_SERVICE);
        if (am == null) return false;
        deviceCallback = new AudioDeviceCallback() {
            @Override public void onAudioDevicesAdded(AudioDeviceInfo[] added) { nativeDevicesChanged(); }
            @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] removed) { nativeDevicesChanged(); }
        };
        am.registerAudioDeviceCallback(deviceCallback, new Handler(Looper.getMainLooper()));
        return true;
    }

    public static void stopDeviceListening() {
        AudioDeviceCallback cb = deviceCallback;
        deviceCallback = null;
        if (cb == null) return;
        Application app = application();
        if (app == null) return;
        AudioManager am = (AudioManager) app.getSystemService(Context.AUDIO_SERVICE);
        if (am != null) am.unregisterAudioDeviceCallback(cb);
    }
}
