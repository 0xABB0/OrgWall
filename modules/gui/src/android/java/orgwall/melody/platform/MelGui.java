package orgwall.melody.platform;

import android.app.Activity;
import android.app.Fragment;
import android.app.FragmentManager;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.util.HashMap;

public final class MelGui {

    private static Activity activity;
    private static FrameLayout container;
    private static int containerId;
    private static float density = 1.0f;
    private static final HashMap<Long, View> views = new HashMap<>();

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
        nativeRegister();
        nativeStart();
    }

    public static void stop() {
        nativeStop();
        views.clear();
        activity    = null;
        container   = null;
        containerId = 0;
        density     = 1.0f;
    }

    public static Activity activity() { return activity; }
    public static float    density()  { return density; }

    public static int dp2px(int dp) { return Math.round(dp * density); }
    public static int px2dp(int px) { return Math.round(px / density); }

    public static void registerView(long handle, View v) { views.put(handle, v); }
    public static View viewFor      (long handle)        { return views.get(handle); }

    public static void attach(View v, long parent, int x, int y, int w, int h) {
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(dp2px(w), dp2px(h));
        lp.leftMargin = dp2px(x);
        lp.topMargin  = dp2px(y);
        v.setLayoutParams(lp);
        View p = views.get(parent);
        if (p instanceof ViewGroup) ((ViewGroup) p).addView(v);
    }

    public static void destroyView(long handle) {
        View v = views.remove(handle);
        if (v == null) return;
        ViewGroup p = (ViewGroup) v.getParent();
        if (p != null) p.removeView(v);
    }

    public static void setText(long handle, String text) {
        View v = views.get(handle);
        if (v instanceof TextView) ((TextView) v).setText(text);
    }

    public static String getText(long handle) {
        View v = views.get(handle);
        return v instanceof TextView ? ((TextView) v).getText().toString() : "";
    }

    public static void setBounds(long handle, int x, int y, int w, int h) {
        View v = views.get(handle);
        if (v == null) return;
        if (v instanceof FrameLayout && v.getParent() == container) return;
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(dp2px(w), dp2px(h));
        lp.leftMargin = dp2px(x);
        lp.topMargin  = dp2px(y);
        v.setLayoutParams(lp);
    }

    public static void setVisible(long handle, boolean visible) {
        View v = views.get(handle);
        if (v != null) v.setVisibility(visible ? View.VISIBLE : View.GONE);
    }

    public static void setEnabled(long handle, boolean enabled) {
        View v = views.get(handle);
        if (v != null) v.setEnabled(enabled);
    }

    public static void setFocus(long handle) {
        View v = views.get(handle);
        if (v != null) v.requestFocus();
    }

    public static void invalidate(long handle) {
        View v = views.get(handle);
        if (v != null) v.invalidate();
    }

    @SuppressWarnings("deprecation")
    public static void presentFrame(long handle, String title) {
        if (activity == null) return;
        FragmentManager fm = activity.getFragmentManager();
        Fragment current   = fm.findFragmentById(containerId);
        if (current instanceof MelScreenFragment
                && ((MelScreenFragment) current).handle() == handle) {
            if (title != null) activity.setTitle(title);
            return;
        }
        fm.beginTransaction()
          .replace(containerId, MelScreenFragment.forHandle(handle))
          .addToBackStack(null)
          .commit();
        if (title != null) activity.setTitle(title);
    }

    public static void setActivityTitle(String title) {
        if (activity != null && title != null) activity.setTitle(title);
    }

    public static native void nativeRegister();
    public static native void nativeStart();
    public static native void nativeStop();
    public static native void nativeFireFocus (long handle, boolean focusedIn);
    public static native void nativeFireResize(long handle, int w, int h);
}
