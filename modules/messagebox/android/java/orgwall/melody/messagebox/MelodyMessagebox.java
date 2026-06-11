package orgwall.melody.messagebox;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Button;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

public final class MelodyMessagebox
{
    private MelodyMessagebox() {}

    public static int show(final Context context,
                           final String title,
                           final String message,
                           final String[] labels,
                           final int defaultIndex,
                           final int cancelIndex,
                           final boolean rightToLeft,
                           final boolean hasAccent, final int accent,
                           final boolean hasText, final int text,
                           final boolean hasBackground, final int background)
    {
        if (context == null || labels == null || labels.length == 0)
            return -1;

        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicInteger result = new AtomicInteger(cancelIndex >= 0 ? cancelIndex : 0);

        final Runnable build = new Runnable()
        {
            @Override
            public void run()
            {
                AlertDialog.Builder b = new AlertDialog.Builder(context);
                if (title != null && title.length() > 0)
                    b.setTitle(title);
                b.setMessage(message != null ? message : "");
                b.setCancelable(cancelIndex >= 0);

                final int n = labels.length;
                final int positiveIdx = 0;
                final int negativeIdx = n >= 2 ? n - 1 : -1;
                final int neutralIdx = n >= 3 ? 1 : -1;

                DialogInterface.OnClickListener click = new DialogInterface.OnClickListener()
                {
                    @Override
                    public void onClick(DialogInterface dialog, int which)
                    {
                        int idx;
                        if (which == DialogInterface.BUTTON_POSITIVE)
                            idx = positiveIdx;
                        else if (which == DialogInterface.BUTTON_NEGATIVE)
                            idx = negativeIdx;
                        else
                            idx = neutralIdx;
                        if (idx >= 0 && idx < labels.length)
                            result.set(idx);
                        dialog.dismiss();
                    }
                };

                if (n >= 1)
                    b.setPositiveButton(labels[positiveIdx], click);
                if (negativeIdx >= 0)
                    b.setNegativeButton(labels[negativeIdx], click);
                if (neutralIdx >= 0)
                    b.setNeutralButton(labels[neutralIdx], click);

                b.setOnCancelListener(new DialogInterface.OnCancelListener()
                {
                    @Override
                    public void onCancel(DialogInterface dialog)
                    {
                        if (cancelIndex >= 0 && cancelIndex < labels.length)
                            result.set(cancelIndex);
                        latch.countDown();
                    }
                });
                b.setOnDismissListener(new DialogInterface.OnDismissListener()
                {
                    @Override
                    public void onDismiss(DialogInterface dialog)
                    {
                        latch.countDown();
                    }
                });

                final AlertDialog dialog = b.create();
                if (hasBackground && dialog.getWindow() != null)
                    dialog.getWindow().setBackgroundDrawable(new ColorDrawable(background));
                if (rightToLeft && dialog.getWindow() != null)
                    dialog.getWindow().getDecorView().setLayoutDirection(View.LAYOUT_DIRECTION_RTL);

                dialog.setOnShowListener(new DialogInterface.OnShowListener()
                {
                    @Override
                    public void onShow(DialogInterface d)
                    {
                        tint(dialog.getButton(DialogInterface.BUTTON_POSITIVE), hasAccent, accent, hasText, text);
                        tint(dialog.getButton(DialogInterface.BUTTON_NEGATIVE), hasAccent, accent, hasText, text);
                        tint(dialog.getButton(DialogInterface.BUTTON_NEUTRAL), hasAccent, accent, hasText, text);
                    }
                });

                dialog.show();
            }
        };

        Handler handler = new Handler(Looper.getMainLooper());
        if (Looper.myLooper() == Looper.getMainLooper())
        {
            build.run();
            return result.get();
        }

        handler.post(build);
        try
        {
            latch.await();
        }
        catch (InterruptedException e)
        {
            Thread.currentThread().interrupt();
        }
        return result.get();
    }

    private static void tint(Button button, boolean hasAccent, int accent, boolean hasText, int text)
    {
        if (button == null)
            return;
        if (hasText)
            button.setTextColor(text);
        else if (hasAccent)
            button.setTextColor(accent);
    }
}
