package orgwall.melody.platform;

import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

public final class MelFrame {

    private MelFrame() {}

    public static View create(long handle, String title, long fnResize) {
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
        return frame;
    }

    public static native void nativeResize(long handle, long fn, int w, int h);
}
