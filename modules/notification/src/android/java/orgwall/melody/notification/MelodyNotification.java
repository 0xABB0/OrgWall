package orgwall.melody.notification;

import android.app.Activity;
import android.app.Application;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.RemoteInput;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

import java.util.HashMap;

public final class MelodyNotification
{
    private MelodyNotification() {}

    private static final String PERMISSION = "android.permission.POST_NOTIFICATIONS";
    private static final int REQUEST_CODE = 0x4D4E;

    private static volatile Activity current;
    private static boolean hooked;
    private static final Handler handler = new Handler(Looper.getMainLooper());
    private static final HashMap<Long, Runnable> scheduled = new HashMap<Long, Runnable>();
    private static int requestSeq = 1;

    private static synchronized void hook(Context ctx)
    {
        if (hooked)
            return;
        Context app = ctx.getApplicationContext();
        if (app instanceof Application)
        {
            ((Application) app).registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
                @Override public void onActivityResumed(Activity a) { current = a; }
                @Override public void onActivityPaused(Activity a) { if (current == a) current = null; }
                @Override public void onActivityCreated(Activity a, Bundle b) {}
                @Override public void onActivityStarted(Activity a) {}
                @Override public void onActivityStopped(Activity a) {}
                @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
                @Override public void onActivityDestroyed(Activity a) { if (current == a) current = null; }
            });
            hooked = true;
        }
    }

    private static Activity resolveActivity()
    {
        Activity a = current;
        if (a != null)
            return a;
        try
        {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Object thread = at.getMethod("currentActivityThread").invoke(null);
            Object app = at.getMethod("getApplication").invoke(thread);
            if (app instanceof Application)
                hook((Application) app);
        }
        catch (Throwable t)
        {
        }
        return current;
    }

    private static NotificationManager manager(Context ctx)
    {
        return (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
    }

    private static int notifId(long token)
    {
        return (int) (token ^ (token >>> 32));
    }

    public static boolean enabled(Context ctx)
    {
        hook(ctx);
        return manager(ctx).areNotificationsEnabled();
    }

    public static boolean needsRuntimePermission(Context ctx)
    {
        if (Build.VERSION.SDK_INT < 33)
            return false;
        return ctx.checkSelfPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED;
    }

    public static boolean requestPermission()
    {
        final Activity activity = resolveActivity();
        if (activity == null)
            return false;
        handler.post(new Runnable() {
            @Override public void run()
            {
                activity.requestPermissions(new String[] { PERMISSION }, REQUEST_CODE);
            }
        });
        return true;
    }

    public static void ensureChannel(Context ctx, String id, String label, String desc, boolean high, boolean silent)
    {
        if (Build.VERSION.SDK_INT < 26)
            return;
        int importance = high ? NotificationManager.IMPORTANCE_HIGH : (silent ? NotificationManager.IMPORTANCE_LOW : NotificationManager.IMPORTANCE_DEFAULT);
        NotificationChannel channel = new NotificationChannel(id, label, importance);
        if (desc != null && desc.length() > 0)
            channel.setDescription(desc);
        if (silent)
            channel.setSound(null, null);
        manager(ctx).createNotificationChannel(channel);
    }

    private static Bitmap bitmapOf(int[] argb, int w, int h)
    {
        if (argb == null || w <= 0 || h <= 0)
            return null;
        return Bitmap.createBitmap(argb, w, h, Bitmap.Config.ARGB_8888);
    }

    private static PendingIntent broadcast(Context ctx, long token, String action, boolean dismissed, boolean foreground, boolean mutable)
    {
        Intent intent = new Intent(ctx, MelodyNotificationReceiver.class);
        intent.putExtra("melody.token", token);
        intent.putExtra("melody.action", action);
        intent.putExtra("melody.dismissed", dismissed);
        intent.putExtra("melody.foreground", foreground);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23)
            flags |= mutable ? (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0) : PendingIntent.FLAG_IMMUTABLE;
        int req;
        synchronized (MelodyNotification.class)
        {
            req = requestSeq++;
        }
        return PendingIntent.getBroadcast(ctx, req, intent, flags);
    }

    public static void post(final Context ctx, final long token, final String channel,
                            final String title, final String subtitle, final String body, final String group,
                            final int[] iconArgb, final int iconW, final int iconH, final String iconPath,
                            final int[] imageArgb, final int imageW, final int imageH, final String imagePath,
                            final String[] actionIds, final String[] actionLabels, final int[] actionFlags,
                            final boolean hasProgress, final boolean indeterminate, final int progressPct,
                            final boolean hasBadge, final int badge,
                            final long delayMs, final long intervalMs)
    {
        hook(ctx);
        final Runnable show = new Runnable() {
            @Override public void run()
            {
                Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(ctx, channel) : new Notification.Builder(ctx);
                int small = ctx.getApplicationInfo().icon;
                b.setSmallIcon(small != 0 ? small : android.R.drawable.ic_dialog_info);
                if (title != null && title.length() > 0)
                    b.setContentTitle(title);
                if (body != null && body.length() > 0)
                    b.setContentText(body);
                if (subtitle != null && subtitle.length() > 0)
                    b.setSubText(subtitle);
                if (group != null && group.length() > 0)
                    b.setGroup(group);

                Bitmap largeIcon = bitmapOf(iconArgb, iconW, iconH);
                if (largeIcon != null)
                    b.setLargeIcon(largeIcon);
                else if (iconPath != null && iconPath.length() > 0)
                {
                    Bitmap fromPath = android.graphics.BitmapFactory.decodeFile(iconPath);
                    if (fromPath != null)
                        b.setLargeIcon(fromPath);
                }

                Bitmap big = bitmapOf(imageArgb, imageW, imageH);
                if (big == null && imagePath != null && imagePath.length() > 0)
                    big = android.graphics.BitmapFactory.decodeFile(imagePath);
                if (big != null)
                    b.setStyle(new Notification.BigPictureStyle().bigPicture(big));

                if (hasProgress)
                    b.setProgress(100, progressPct, indeterminate);
                if (hasBadge)
                    b.setNumber(badge);

                b.setContentIntent(broadcast(ctx, token, "", false, true, false));
                b.setDeleteIntent(broadcast(ctx, token, "", true, false, false));
                b.setAutoCancel(true);

                if (actionIds != null)
                {
                    for (int i = 0; i < actionIds.length; i++)
                    {
                        boolean textInput = (actionFlags[i] & 4) != 0;
                        boolean foreground = (actionFlags[i] & 1) != 0;
                        PendingIntent pi = broadcast(ctx, token, actionIds[i], false, foreground, textInput);
                        Notification.Action.Builder ab = new Notification.Action.Builder((Icon) null, actionLabels[i], pi);
                        if (textInput)
                            ab.addRemoteInput(new RemoteInput.Builder("melody.reply").setLabel(actionLabels[i]).build());
                        b.addAction(ab.build());
                    }
                }

                manager(ctx).notify(notifId(token), b.build());
            }
        };

        synchronized (scheduled)
        {
            Runnable prior = scheduled.remove(token);
            if (prior != null)
                handler.removeCallbacks(prior);
        }

        if (delayMs <= 0 && intervalMs <= 0)
        {
            handler.post(show);
            return;
        }

        Runnable timed = new Runnable() {
            @Override public void run()
            {
                show.run();
                if (intervalMs > 0)
                {
                    handler.postDelayed(this, intervalMs);
                }
                else
                {
                    synchronized (scheduled)
                    {
                        scheduled.remove(token);
                    }
                }
            }
        };
        synchronized (scheduled)
        {
            scheduled.put(token, timed);
        }
        handler.postDelayed(timed, delayMs > 0 ? delayMs : intervalMs);
    }

    public static void cancel(Context ctx, long token)
    {
        synchronized (scheduled)
        {
            Runnable prior = scheduled.remove(token);
            if (prior != null)
                handler.removeCallbacks(prior);
        }
        manager(ctx).cancel(notifId(token));
    }

    public static void cancelAll(Context ctx)
    {
        synchronized (scheduled)
        {
            for (Runnable r : scheduled.values())
                handler.removeCallbacks(r);
            scheduled.clear();
        }
        manager(ctx).cancelAll();
    }
}
