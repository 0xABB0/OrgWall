package orgwall.melody.platform;

import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

public final class MelFrame {

    private MelFrame() {}

    public static View create(long handle, String title) {
        FrameLayout frame = new FrameLayout(MelGui.activity());
        frame.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        frame.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or_, ob) -> {
            int w = r - l;
            int h = b - t;
            if (w != or_ - ol || h != ob - ot) {
                MelGui.nativeFireResize(handle, MelGui.px2dp(w), MelGui.px2dp(h));
            }
        });
        MelGui.registerView(handle, frame);
        return frame;
    }
}
