package orgwall.melody.platform;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.WindowManager;
import android.widget.Toast;

import java.util.HashMap;

public final class MelodyPlatform
{
    public static final int DEVICE_UNKNOWN = 0;
    public static final int DEVICE_PHONE   = 1;
    public static final int DEVICE_TABLET  = 2;
    public static final int DEVICE_TV       = 4;

    private static final HashMap<Integer, Long> sPermissionTokens = new HashMap<>();
    private static int sNextRequest = 0x4d454c10;

    private MelodyPlatform() {}

    private static native void nativePermissionResult(long token, boolean granted);

    public static int deviceClass(Activity activity)
    {
        Configuration cfg = activity.getResources().getConfiguration();
        if ((cfg.uiMode & Configuration.UI_MODE_TYPE_MASK) == Configuration.UI_MODE_TYPE_TELEVISION)
            return DEVICE_TV;
        if (cfg.smallestScreenWidthDp >= 600)
            return DEVICE_TABLET;
        return DEVICE_PHONE;
    }

    public static void setKeepScreenOn(final Activity activity, final boolean on)
    {
        activity.runOnUiThread(new Runnable()
        {
            public void run()
            {
                if (on)
                    activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                else
                    activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        });
    }

    public static void toast(final Activity activity, final String text, final boolean longDuration)
    {
        activity.runOnUiThread(new Runnable()
        {
            public void run()
            {
                Toast.makeText(activity, text, longDuration ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show();
            }
        });
    }

    public static String internalStoragePath(Activity activity)
    {
        return activity.getFilesDir().getAbsolutePath();
    }

    public static String externalStoragePath(Activity activity)
    {
        java.io.File dir = activity.getExternalFilesDir(null);
        return dir != null ? dir.getAbsolutePath() : null;
    }

    public static String cachePath(Activity activity)
    {
        return activity.getCacheDir().getAbsolutePath();
    }

    public static void requestPermission(final Activity activity, final String permission, final long token)
    {
        if (activity.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED)
        {
            nativePermissionResult(token, true);
            return;
        }
        final int request;
        synchronized (sPermissionTokens)
        {
            request = sNextRequest++;
            sPermissionTokens.put(request, token);
        }
        activity.runOnUiThread(new Runnable()
        {
            public void run()
            {
                activity.requestPermissions(new String[] { permission }, request);
            }
        });
    }

    public static void onRequestPermissionsResult(int requestCode, int[] grantResults)
    {
        Long token;
        synchronized (sPermissionTokens)
        {
            token = sPermissionTokens.remove(requestCode);
        }
        if (token == null)
            return;
        boolean granted = grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED;
        nativePermissionResult(token, granted);
    }
}
