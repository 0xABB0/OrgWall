package orgwall.melody.platform;

import android.view.View;
import android.widget.SeekBar;

public final class MelSlider {

    private MelSlider() {}

    public static View create(long handle, long parent, int x, int y, int w, int h,
                              int min, int max, int val) {
        SeekBar v = new SeekBar(MelGui.activity());
        v.setMax(max - min);
        v.setProgress(val - min);
        v.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                if (fromUser) nativeValueChanged(handle, progress);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch (SeekBar sb) {}
        });
        v.setOnFocusChangeListener((view, hasFocus) -> MelGui.nativeFireFocus(handle, hasFocus));
        MelGui.attach(v, parent, x, y, w, h);
        MelGui.registerView(handle, v);
        return v;
    }

    public static int value(long handle) {
        View v = MelGui.viewFor(handle);
        return v instanceof SeekBar ? ((SeekBar) v).getProgress() : 0;
    }

    public static void setValue(long handle, int val) {
        View v = MelGui.viewFor(handle);
        if (v instanceof SeekBar) ((SeekBar) v).setProgress(val);
    }

    public static native void nativeValueChanged(long handle, int value);
}
