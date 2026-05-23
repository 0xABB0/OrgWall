package orgwall.melody.platform;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.HashMap;

public final class MelGui {

    private static Activity activity;
    private static FrameLayout root;
    private static final HashMap<Long, View>   views  = new HashMap<>();
    private static final HashMap<Long, String> titles = new HashMap<>();

    private MelGui() {}

    public static void start(Activity act, FrameLayout container) {
        activity = act;
        root = container;
        nativeRegister();
        nativeStart();
    }

    public static void stop() {
        nativeStop();
    }

    public static void createFrame(long handle, String title) {
        FrameLayout frame = new FrameLayout(activity);
        frame.setVisibility(View.GONE);
        frame.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        views.put(handle, frame);
        titles.put(handle, title);
        root.addView(frame);
    }

    public static void createLabel(long handle, long parent, int x, int y, int w, int h, String text) {
        TextView v = new TextView(activity);
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        attach(v, parent, x, y, w, h);
        views.put(handle, v);
    }

    public static void createButton(long handle, long parent, int x, int y, int w, int h, String text) {
        Button v = new Button(activity);
        v.setText(text);
        v.setAllCaps(false);
        v.setOnClickListener(view -> nativeFireClick(handle));
        v.setOnFocusChangeListener((view, hasFocus) -> nativeFireFocus(handle, hasFocus));
        attach(v, parent, x, y, w, h);
        views.put(handle, v);
    }

    public static void createCheckBox(long handle, long parent, int x, int y, int w, int h,
                                      String text, boolean checked) {
        CheckBox v = new CheckBox(activity);
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        v.setChecked(checked);
        v.setOnCheckedChangeListener((CompoundButton btn, boolean isChecked)
                -> nativeFireToggled(handle, isChecked));
        v.setOnFocusChangeListener((view, hasFocus) -> nativeFireFocus(handle, hasFocus));
        attach(v, parent, x, y, w, h);
        views.put(handle, v);
    }

    public static void createTextField(long handle, long parent, int x, int y, int w, int h, String text) {
        EditText v = new EditText(activity);
        v.setSingleLine(true);
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        v.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int st, int c, int a) {}
            @Override public void onTextChanged(CharSequence s, int st, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                nativeFireTextChanged(handle, s.toString());
            }
        });
        v.setOnFocusChangeListener((view, hasFocus) -> nativeFireFocus(handle, hasFocus));
        attach(v, parent, x, y, w, h);
        views.put(handle, v);
    }

    public static void createSlider(long handle, long parent, int x, int y, int w, int h,
                                    int min, int max, int val) {
        SeekBar v = new SeekBar(activity);
        final int base = min;
        v.setMax(max - min);
        v.setProgress(val - min);
        v.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                nativeFireSliderChanged(handle, progress + base);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });
        v.setOnFocusChangeListener((view, hasFocus) -> nativeFireFocus(handle, hasFocus));
        attach(v, parent, x, y, w, h);
        views.put(handle, v);
    }

    public static void createCanvas(long handle, long parent, int x, int y, int w, int h) {
        MelCanvasView v = new MelCanvasView(activity, handle);
        attach(v, parent, x, y, w, h);
        views.put(handle, v);
    }

    public static void destroyView(long handle) {
        View v = views.remove(handle);
        titles.remove(handle);
        if (v == null) return;
        ViewGroup p = (ViewGroup) v.getParent();
        if (p != null) p.removeView(v);
    }

    public static void setText(long handle, String text) {
        View v = views.get(handle);
        if (v instanceof TextView) {
            ((TextView) v).setText(text);
        } else if (v instanceof FrameLayout) {
            titles.put(handle, text);
            if (v.getVisibility() == View.VISIBLE && activity != null) {
                activity.setTitle(text);
            }
        }
    }

    public static String getText(long handle) {
        View v = views.get(handle);
        if (v instanceof TextView) return ((TextView) v).getText().toString();
        if (v instanceof FrameLayout) {
            String t = titles.get(handle);
            return t != null ? t : "";
        }
        return "";
    }

    public static void setBounds(long handle, int x, int y, int w, int h) {
        View v = views.get(handle);
        if (v == null) return;
        if (v instanceof FrameLayout && v.getParent() == root) return;
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h);
        lp.leftMargin = x;
        lp.topMargin  = y;
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

    public static int sliderValue(long handle) {
        View v = views.get(handle);
        if (v instanceof SeekBar) return ((SeekBar) v).getProgress();
        return 0;
    }

    public static void setSliderValue(long handle, int val) {
        View v = views.get(handle);
        if (v instanceof SeekBar) ((SeekBar) v).setProgress(val);
    }

    public static boolean checkBoxChecked(long handle) {
        View v = views.get(handle);
        if (v instanceof CheckBox) return ((CheckBox) v).isChecked();
        return false;
    }

    public static void presentFrame(long handle) {
        for (int i = 0; i < root.getChildCount(); i++) {
            View child = root.getChildAt(i);
            child.setVisibility(View.GONE);
        }
        View v = views.get(handle);
        if (v != null) {
            v.setVisibility(View.VISIBLE);
            String t = titles.get(handle);
            if (t != null) activity.setTitle(t);
        }
    }

    private static void attach(View v, long parent, int x, int y, int w, int h) {
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h);
        lp.leftMargin = x;
        lp.topMargin  = y;
        v.setLayoutParams(lp);
        View p = views.get(parent);
        if (p instanceof ViewGroup) ((ViewGroup) p).addView(v);
    }

    public static native void nativeRegister();
    public static native void nativeStart();
    public static native void nativeStop();
    public static native void nativeFireClick(long handle);
    public static native void nativeFireFocus(long handle, boolean focusedIn);
    public static native void nativeFireToggled(long handle, boolean checked);
    public static native void nativeFireSliderChanged(long handle, int value);
    public static native void nativeFireTextChanged(long handle, String text);
    public static native void nativeFirePointer(long handle, int kind, int x, int y);
    public static native void nativeFireKey(long handle, int key, boolean down);
    public static native void nativeFireCanvasPaint(long handle, Canvas canvas, int w, int h);
}
