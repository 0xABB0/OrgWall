package orgwall.melody.notification;

import android.app.RemoteInput;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

public final class MelodyNotificationReceiver extends BroadcastReceiver
{
    public static volatile boolean nativeReady;

    public static native void nativeEvent(long token, String action, String reply, boolean dismissed);

    @Override
    public void onReceive(Context context, Intent intent)
    {
        long token = intent.getLongExtra("melody.token", 0);
        String action = intent.getStringExtra("melody.action");
        boolean dismissed = intent.getBooleanExtra("melody.dismissed", false);
        boolean foreground = intent.getBooleanExtra("melody.foreground", false);

        String reply = null;
        Bundle remote = RemoteInput.getResultsFromIntent(intent);
        if (remote != null)
        {
            CharSequence cs = remote.getCharSequence("melody.reply");
            if (cs != null)
                reply = cs.toString();
        }

        if (foreground && !dismissed)
        {
            Intent launch = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
            if (launch != null)
            {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(launch);
            }
        }

        if (nativeReady)
            nativeEvent(token, action != null ? action : "", reply != null ? reply : "", dismissed);
    }
}
