package orgwall.melody.platform;

import android.app.Activity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.FrameLayout;

import java.util.ArrayDeque;

public final class MelGui {

    public interface BackHost { void onBackAvailable(boolean canGoBack); }

    private static Activity   activity;
    private static FrameLayout container;
    private static float      density = 1.0f;
    private static BackHost   backHost;

    private static final ArrayDeque<View> stack = new ArrayDeque<>();

    private MelGui() {}

    public static void setBackHost(BackHost h) { backHost = h; }

    public static void start(Activity act, FrameLayout root) {
        activity  = act;
        density   = act.getResources().getDisplayMetrics().density;
        container = new FrameLayout(act);
        root.addView(container, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        nativeRegister(density);
        nativeStart();
    }

    public static void stop() {
        nativeStop();
        stack.clear();
        if (container != null) container.removeAllViews();
        activity  = null;
        container = null;
        density   = 1.0f;
        backHost  = null;
    }

    public static Activity activity() { return activity; }
    public static float    density()  { return density; }
    public static int      px2dp(int px) { return Math.round(px / density); }
    public static int      dp2px(int dp) { return Math.round(dp * density); }

    public static void setActivityTitle(String title) {
        if (activity != null && title != null) activity.setTitle(title);
    }

    /* Shared focus listener: installed by C only when a focus slot is set. The
     * View itself carries the two C handler pointers (as longs). */
    public static void installFocus(View v, long handle, long fnIn, long fnOut) {
        v.setOnFocusChangeListener((view, hasFocus) -> nativeFocus(handle, hasFocus, fnIn, fnOut));
    }

    /* Navigate to frameView: if it is already on the stack, collapse to it;
     * otherwise push it. The container shows exactly the top of the stack. */
    public static void presentFrame(View frameView) {
        if (container == null || frameView == null) return;
        if (stack.contains(frameView)) {
            while (stack.peek() != frameView) stack.pop();
        } else {
            stack.push(frameView);
        }
        showTop();
        notifyBack();
    }

    /* Pop one frame off the stack. Returns false when already at the root, so a
     * caller (the OS back gesture) can let the system finish the activity. */
    public static boolean back() {
        if (stack.size() <= 1) return false;
        stack.pop();
        showTop();
        notifyBack();
        return true;
    }

    public static int backDepth() { return stack.size(); }

    private static void showTop() {
        if (container == null) return;
        View top = stack.peek();
        View cur = container.getChildCount() > 0 ? container.getChildAt(0) : null;
        if (cur != top) {
            if (cur != null) container.removeView(cur);
            if (top != null) {
                ViewParent p = top.getParent();
                if (p instanceof ViewGroup) ((ViewGroup) p).removeView(top);
                container.addView(top, new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT));
                top.requestApplyInsets();
            }
        }
        if (top != null) {
            Object tag = top.getTag();
            if (activity != null && tag != null) activity.setTitle(tag.toString());
        }
    }

    private static void notifyBack() {
        if (backHost != null) backHost.onBackAvailable(stack.size() > 1);
    }

    public static native void nativeRegister(float density);
    public static native void nativeStart();
    public static native void nativeStop();
    public static native void nativeFocus(long handle, boolean in, long fnIn, long fnOut);
}
