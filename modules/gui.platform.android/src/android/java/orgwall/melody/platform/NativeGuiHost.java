package orgwall.melody.platform;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.util.Map;
import java.util.WeakHashMap;

public final class NativeGuiHost {
    public static final int SWP_NOMOVE  = 1 << 0;
    public static final int SWP_NOSIZE  = 1 << 1;
    public static final int SWP_SHOW    = 1 << 2;
    public static final int SWP_HIDE    = 1 << 3;
    public static final int SWP_ENABLE  = 1 << 4;
    public static final int SWP_DISABLE = 1 << 5;

    private static final int DEFAULT_POSITION = Integer.MIN_VALUE;

    private final Activity activity;
    private final FrameLayout root;
    private final Handler mainHandler;
    private final Map<EditText, TextWatcher> editWatchers = new WeakHashMap<>();
    private final Map<View, Long> nativeHandles = new WeakHashMap<>();

    public NativeGuiHost(Activity activity, FrameLayout root) {
        this.activity = activity;
        this.root = root;
        this.mainHandler = new Handler(Looper.getMainLooper());
    }

    public Activity getActivity() { return activity; }
    public View getRoot() { return root; }

    public void attach(View parent, View view, int x, int y, int w, int h) {
        if (view == null || !(parent instanceof ViewGroup) || view.getParent() != null) return;
        int width  = (w > 0) ? dp(w) : ViewGroup.LayoutParams.MATCH_PARENT;
        int height = (h > 0) ? dp(h) : ViewGroup.LayoutParams.MATCH_PARENT;
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(width, height);
        params.leftMargin = (x == DEFAULT_POSITION) ? 0 : dp(x);
        params.topMargin  = (y == DEFAULT_POSITION) ? 0 : dp(y);
        ((ViewGroup) parent).addView(view, params);
    }

    public void detach(View view) {
        if (view == null) return;
        ViewGroup parent = (ViewGroup) view.getParent();
        if (parent != null) parent.removeView(view);
    }

    public String getText(View view) {
        if (view instanceof TextView) {
            CharSequence cs = ((TextView) view).getText();
            return cs == null ? "" : cs.toString();
        }
        return null;
    }

    public void setText(View view, String text) {
        if (!(view instanceof TextView)) return;
        if (view instanceof EditText) {
            EditText e = (EditText) view;
            TextWatcher w = editWatchers.get(e);
            if (w != null) {
                e.removeTextChangedListener(w);
                e.setText(text);
                e.addTextChangedListener(w);
                return;
            }
        }
        ((TextView) view).setText(text);
    }

    public void setWindowPos(View view, int x, int y, int w, int h, int flags) {
        if (view == null) return;
        boolean moveOrSize = (flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE);
        if (moveOrSize && view.getParent() instanceof FrameLayout) {
            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) view.getLayoutParams();
            if (params == null) params = new FrameLayout.LayoutParams(dp(w), dp(h));
            if ((flags & SWP_NOSIZE) == 0) {
                params.width  = (w > 0) ? dp(w) : ViewGroup.LayoutParams.MATCH_PARENT;
                params.height = (h > 0) ? dp(h) : ViewGroup.LayoutParams.MATCH_PARENT;
            }
            if ((flags & SWP_NOMOVE) == 0) {
                params.leftMargin = (x == DEFAULT_POSITION) ? 0 : dp(x);
                params.topMargin  = (y == DEFAULT_POSITION) ? 0 : dp(y);
            }
            view.setLayoutParams(params);
        }
        if ((flags & SWP_SHOW)    != 0) view.setVisibility(View.VISIBLE);
        if ((flags & SWP_HIDE)    != 0) view.setVisibility(View.GONE);
        if ((flags & SWP_ENABLE)  != 0) view.setEnabled(true);
        if ((flags & SWP_DISABLE) != 0) view.setEnabled(false);
    }

    public void bindNativeHandle(View view, long handle) {
        if (view != null) nativeHandles.put(view, Long.valueOf(handle));
    }

    public void bindEditWatcher(EditText e, TextWatcher w) {
        if (e != null && w != null) editWatchers.put(e, w);
    }

    public void scheduleStartActivity(String activityName) {
        mainHandler.post(() -> nativeStartActivity(activityName));
    }

    public void requestExit() {
        activity.finish();
    }

    public void post(final long handle, final int msg, final long wparam, final long lparam) {
        mainHandler.post(() -> nativeDispatchPosted(handle, msg, wparam, lparam));
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP,
                value,
                activity.getResources().getDisplayMetrics());
    }

    static native void dispatchClick(long handle);
    static native void dispatchValueChanged(long handle, int value);
    static native void dispatchTextChanged(long handle, String value);
    static native void dispatchFocus(long handle, boolean focused);
    static native boolean dispatchKey(long handle, int keyCode, boolean down);
    static native boolean dispatchPointer(long handle, int action, int x, int y);
    static native void dispatchPaint(long handle, android.graphics.Canvas canvas);

    private static native void nativeDispatchPosted(long handle, int msg, long wparam, long lparam);
    private static native void nativeStartActivity(String name);
}
