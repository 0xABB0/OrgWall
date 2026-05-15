package orgwall.melody.platform;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.util.TypedValue;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.SeekBar;
import android.widget.TextView;

public final class NativeGuiHost {
    private static final int FG = Color.rgb(245, 241, 232);
    private static final int ACCENT = Color.rgb(255, 190, 96);

    private final Activity activity;
    private final FrameLayout root;

    public NativeGuiHost(Activity activity, FrameLayout root) {
        this.activity = activity;
        this.root = root;
    }

    public View create(String className, String text, int x, int y, int width, int height, long parent, int id) {
        View view;

        switch (className) {
            case "mel.window":
                return root;
            case "mel.label":
                TextView label = new TextView(activity);
                label.setText(text);
                label.setTextColor(FG);
                label.setTextSize(id == 1 ? 26.0f : 15.0f);
                label.setGravity(Gravity.CENTER_VERTICAL);
                view = label;
                break;
            case "mel.button":
                Button button = new Button(activity);
                button.setText(text);
                button.setAllCaps(false);
                button.setOnClickListener(v -> nativeDispatchClick(nativeHandle(v)));
                view = button;
                break;
            case "mel.edit":
                EditText edit = new EditText(activity);
                edit.setText(text);
                edit.setSingleLine(true);
                edit.setTextColor(FG);
                edit.setHintTextColor(Color.rgb(170, 178, 186));
                edit.setTextSize(15.0f);
                edit.setSelectAllOnFocus(false);
                edit.setBackgroundColor(Color.rgb(38, 51, 65));
                edit.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                    @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                        nativeDispatchTextChanged(nativeHandle(edit), s.toString());
                    }
                    @Override public void afterTextChanged(Editable s) {}
                });
                view = edit;
                break;
            case "mel.checkbox":
                CheckBox checkBox = new CheckBox(activity);
                checkBox.setText(text);
                checkBox.setTextColor(FG);
                checkBox.setTextSize(15.0f);
                checkBox.setOnCheckedChangeListener((buttonView, checked) ->
                        nativeDispatchValueChanged(nativeHandle(buttonView), checked ? 1 : 0));
                view = checkBox;
                break;
            case "mel.slider":
                SeekBar slider = new SeekBar(activity);
                slider.setMax(100);
                slider.setProgress(65);
                slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                        nativeDispatchValueChanged(nativeHandle(seekBar), progress);
                    }
                    @Override public void onStartTrackingTouch(SeekBar seekBar) {}
                    @Override public void onStopTrackingTouch(SeekBar seekBar) {}
                });
                view = slider;
                break;
            default:
                TextView fallback = new TextView(activity);
                fallback.setText("Unknown class: " + className);
                fallback.setTextColor(ACCENT);
                view = fallback;
                break;
        }

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(dp(width), dp(height));
        params.leftMargin = dp(x);
        params.topMargin = dp(y);
        root.addView(view, params);
        return view;
    }

    public void bindNativeHandle(View view, long handle) {
        view.setTag(Long.valueOf(handle));
    }

    public void setText(View view, String text) {
        if (view instanceof TextView) {
            ((TextView)view).setText(text);
        }
    }

    public void setRect(View view, int x, int y, int width, int height) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(dp(width), dp(height));
        params.leftMargin = dp(x);
        params.topMargin = dp(y);
        view.setLayoutParams(params);
    }

    public void show(View view, boolean visible) {
        view.setVisibility(visible ? View.VISIBLE : View.GONE);
    }

    public void enable(View view, boolean enabled) {
        view.setEnabled(enabled);
    }

    public void startActivity(String activityName) {
        Intent intent = new Intent(activity, MelodyActivity.class);
        intent.putExtra(MelodyActivity.EXTRA_ACTIVITY_NAME, activityName);
        activity.startActivity(intent);
    }

    private static long nativeHandle(View view) {
        Object tag = view.getTag();
        return tag instanceof Long ? ((Long)tag).longValue() : 0L;
    }

    private int dp(int value) {
        return (int)TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP,
                value,
                activity.getResources().getDisplayMetrics());
    }

    private static native void nativeDispatchClick(long handle);
    private static native void nativeDispatchValueChanged(long handle, int value);
    private static native void nativeDispatchTextChanged(long handle, String value);
}
