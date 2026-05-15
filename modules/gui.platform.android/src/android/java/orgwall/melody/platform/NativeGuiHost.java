package orgwall.melody.platform;

import android.app.Activity;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.SeekBar;
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

    private static final int FG = Color.rgb(245, 241, 232);
    private static final int ACCENT = Color.rgb(255, 190, 96);

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

    public View createWindow() { return root; }

    public View createButton(String text) {
        Button b = new Button(activity);
        b.setText(text);
        b.setAllCaps(false);
        b.setOnClickListener(v -> nativeDispatchClick(nativeHandle(v)));
        return b;
    }

    public View createLabel(String text, int id) {
        TextView t = new TextView(activity);
        t.setText(text);
        t.setTextColor(FG);
        t.setTextSize(id == 1 ? 26.0f : 15.0f);
        t.setGravity(Gravity.CENTER_VERTICAL);
        return t;
    }

    public View createEdit(String text) {
        EditText e = new EditText(activity);
        e.setText(text);
        e.setSingleLine(true);
        e.setTextColor(FG);
        e.setHintTextColor(Color.rgb(170, 178, 186));
        e.setTextSize(15.0f);
        e.setSelectAllOnFocus(false);
        e.setBackgroundColor(Color.rgb(38, 51, 65));
        TextWatcher watcher = new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                nativeDispatchTextChanged(nativeHandle(e), s.toString());
            }
            @Override public void afterTextChanged(Editable s) {}
        };
        e.addTextChangedListener(watcher);
        editWatchers.put(e, watcher);
        return e;
    }

    public View createCheckbox(String text) {
        CheckBox c = new CheckBox(activity);
        c.setText(text);
        c.setTextColor(FG);
        c.setTextSize(15.0f);
        c.setOnCheckedChangeListener((bv, checked) -> nativeDispatchValueChanged(nativeHandle(bv), checked ? 1 : 0));
        return c;
    }

    public View createSlider() {
        SeekBar s = new SeekBar(activity);
        s.setMax(100);
        s.setProgress(65);
        s.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                nativeDispatchValueChanged(nativeHandle(sb), progress);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });
        return s;
    }

    public View createPanel() {
        FrameLayout f = new FrameLayout(activity);
        return f;
    }

    public void attach(View parent, View view, int x, int y, int w, int h) {
        if (view == null || view.getParent() != null) return;
        ViewGroup target = (parent instanceof ViewGroup) ? (ViewGroup) parent : root;
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(dp(w), dp(h));
        params.leftMargin = dp(x);
        params.topMargin = dp(y);
        target.addView(view, params);
    }

    public void detach(View view) {
        if (view == null) return;
        ViewGroup parent = (ViewGroup) view.getParent();
        if (parent != null) parent.removeView(view);
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
            if ((flags & SWP_NOSIZE) == 0) { params.width = dp(w); params.height = dp(h); }
            if ((flags & SWP_NOMOVE) == 0) { params.leftMargin = dp(x); params.topMargin = dp(y); }
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

    public void scheduleStartActivity(String activityName) {
        mainHandler.post(() -> nativeStartActivity(activityName));
    }

    public void requestExit() {
        activity.finish();
    }

    public void post(final long handle, final int msg, final long wparam, final long lparam) {
        mainHandler.post(() -> nativeDispatchPosted(handle, msg, wparam, lparam));
    }

    private long nativeHandle(View view) {
        Long v = nativeHandles.get(view);
        return v == null ? 0L : v.longValue();
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP,
                value,
                activity.getResources().getDisplayMetrics());
    }

    private static native void nativeDispatchClick(long handle);
    private static native void nativeDispatchValueChanged(long handle, int value);
    private static native void nativeDispatchTextChanged(long handle, String value);
    private static native void nativeDispatchPosted(long handle, int msg, long wparam, long lparam);
    private static native void nativeStartActivity(String name);
}
