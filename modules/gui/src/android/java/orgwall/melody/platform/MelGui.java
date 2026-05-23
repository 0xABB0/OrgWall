package orgwall.melody.platform;

import android.app.Activity;
import android.app.Fragment;
import android.app.FragmentManager;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

public final class MelGui {

    private static Activity activity;
    private static FrameLayout container;
    private static int containerId;
    private static float density = 1.0f;

    private MelGui() {}

    public static void start(Activity act, FrameLayout root) {
        activity    = act;
        density     = act.getResources().getDisplayMetrics().density;
        container   = new FrameLayout(act);
        containerId = View.generateViewId();
        container.setId(containerId);
        root.addView(container, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        nativeRegister(density);
        nativeStart();
    }

    public static void stop() {
        nativeStop();
        activity    = null;
        container   = null;
        containerId = 0;
        density     = 1.0f;
    }

    public static Activity activity() { return activity; }
    public static float    density()  { return density; }
    public static int      px2dp(int px) { return Math.round(px / density); }

    public static void setActivityTitle(String title) {
        if (activity != null && title != null) activity.setTitle(title);
    }

    /* Shared focus listener: installed by C only when a focus slot is set. The
     * View itself carries the two C handler pointers (as longs). */
    public static void installFocus(View v, long handle, long fnIn, long fnOut) {
        v.setOnFocusChangeListener((view, hasFocus) -> nativeFocus(handle, hasFocus, fnIn, fnOut));
    }

    @SuppressWarnings("deprecation")
    public static void presentFrame(View frameView, String title) {
        if (activity == null || frameView == null) return;
        FragmentManager fm = activity.getFragmentManager();
        Fragment current   = fm.findFragmentById(containerId);
        if (current instanceof MelScreenFragment
                && ((MelScreenFragment) current).view() == frameView) {
            if (title != null) activity.setTitle(title);
            return;
        }
        fm.beginTransaction()
          .replace(containerId, MelScreenFragment.forView(frameView))
          .addToBackStack(null)
          .commit();
        if (title != null) activity.setTitle(title);
    }

    public static native void nativeRegister(float density);
    public static native void nativeStart();
    public static native void nativeStop();
    public static native void nativeFocus(long handle, boolean in, long fnIn, long fnOut);
}
