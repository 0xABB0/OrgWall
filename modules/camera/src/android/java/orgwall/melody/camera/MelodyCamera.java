package orgwall.melody.camera;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

import java.lang.reflect.Method;

public final class MelodyCamera {
    private static final String PERMISSION = "android.permission.CAMERA";
    private static final int REQUEST_CODE = 0x4D43;
    private static final long POLL_INTERVAL_MS = 100;
    private static final long POLL_TIMEOUT_MS = 30000;

    private static volatile Activity current;
    private static boolean lifecycleHooked;

    private MelodyCamera() {}

    public static native void nativePermissionResult(boolean granted);

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

    private static void hookLifecycle(Application app) {
        if (lifecycleHooked || app == null) return;
        lifecycleHooked = true;
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityResumed(Activity a) { current = a; }
            @Override public void onActivityPaused(Activity a) { if (current == a) current = null; }
            @Override public void onActivityCreated(Activity a, Bundle b) {}
            @Override public void onActivityStarted(Activity a) {}
            @Override public void onActivityStopped(Activity a) {}
            @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
            @Override public void onActivityDestroyed(Activity a) { if (current == a) current = null; }
        });
    }

    private static boolean granted(Context ctx) {
        return ctx != null && ctx.checkSelfPermission(PERMISSION) == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean requestPermission() {
        Application app = application();
        if (app == null) return false;
        hookLifecycle(app);

        if (granted(app)) {
            nativePermissionResult(true);
            return true;
        }

        final Activity activity = current;
        if (activity == null) return false;

        final Handler handler = new Handler(Looper.getMainLooper());
        handler.post(new Runnable() {
            @Override public void run() {
                activity.requestPermissions(new String[] { PERMISSION }, REQUEST_CODE);
            }
        });
        pollResult(app, handler, 0);
        return true;
    }

    private static void pollResult(final Context ctx, final Handler handler, final long elapsed) {
        handler.postDelayed(new Runnable() {
            @Override public void run() {
                if (granted(ctx)) {
                    nativePermissionResult(true);
                    return;
                }
                if (elapsed >= POLL_TIMEOUT_MS) {
                    nativePermissionResult(false);
                    return;
                }
                pollResult(ctx, handler, elapsed + POLL_INTERVAL_MS);
            }
        }, POLL_INTERVAL_MS);
    }
}
