package orgwall.melody.platform;

import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.FrameLayout;

public final class MelFrame {

    private static final int MODE_PAD = 0;

    private MelFrame() {}

    public static View create(long handle, String title, long fnResize,
                              int insetMode, long fnInsets) {
        FrameLayout frame = new FrameLayout(MelGui.activity());
        frame.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        frame.setTag(title);
        frame.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or_, ob) -> {
            int w = r - l;
            int h = b - t;
            if (w != or_ - ol || h != ob - ot) {
                nativeResize(handle, fnResize, MelGui.px2dp(w), MelGui.px2dp(h));
            }
        });
        frame.setOnApplyWindowInsetsListener(
                (v, wi) -> applyInsets(v, wi, handle, insetMode, fnInsets));
        return frame;
    }

    private static WindowInsets applyInsets(View v, WindowInsets wi,
                                            long handle, int insetMode, long fnInsets) {
        int[] px = new int[20];
        fillPx(px, wi);

        if (insetMode == MODE_PAD) v.setPadding(px[0], px[1], px[2], px[3]);

        int[] dp = new int[20];
        for (int i = 0; i < 20; i++) dp[i] = MelGui.px2dp(px[i]);
        nativeInsets(handle, fnInsets, dp);

        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ? WindowInsets.CONSUMED : wi;
    }

    /* Pull side of mel_frame_insets: current insets of v's window, in dp,
     * laid out as the native int[20]. */
    public static int[] insets(View v) {
        int[] dp = new int[20];
        WindowInsets wi = v.getRootWindowInsets();
        if (wi == null) return dp;
        int[] px = new int[20];
        fillPx(px, wi);
        for (int i = 0; i < 20; i++) dp[i] = MelGui.px2dp(px[i]);
        return dp;
    }

    /* px layout: [safe, systemBars, displayCutout, ime, systemGestures], each
     * four ints (left, top, right, bottom). safe = max(systemBars, cutout). */
    private static void fillPx(int[] px, WindowInsets wi) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Insets bars = wi.getInsets(WindowInsets.Type.systemBars());
            Insets cut  = wi.getInsets(WindowInsets.Type.displayCutout());
            Insets ime  = wi.getInsets(WindowInsets.Type.ime());
            Insets gst  = wi.getInsets(WindowInsets.Type.mandatorySystemGestures());
            px[4]  = bars.left; px[5]  = bars.top; px[6]  = bars.right; px[7]  = bars.bottom;
            px[8]  = cut.left;  px[9]  = cut.top;  px[10] = cut.right;  px[11] = cut.bottom;
            px[12] = ime.left;  px[13] = ime.top;  px[14] = ime.right;  px[15] = ime.bottom;
            px[16] = gst.left;  px[17] = gst.top;  px[18] = gst.right;  px[19] = gst.bottom;
            px[0]  = Math.max(bars.left,   cut.left);
            px[1]  = Math.max(bars.top,    cut.top);
            px[2]  = Math.max(bars.right,  cut.right);
            px[3]  = Math.max(bars.bottom, cut.bottom);
        } else {
            fillLegacy(px, wi);
        }
    }

    @SuppressWarnings("deprecation")
    private static void fillLegacy(int[] px, WindowInsets wi) {
        px[4] = wi.getSystemWindowInsetLeft();
        px[5] = wi.getSystemWindowInsetTop();
        px[6] = wi.getSystemWindowInsetRight();
        px[7] = wi.getSystemWindowInsetBottom();
        px[0] = px[4]; px[1] = px[5]; px[2] = px[6]; px[3] = px[7];
    }

    public static native void nativeResize(long handle, long fn, int w, int h);
    public static native void nativeInsets(long handle, long fn, int[] insets);
}
