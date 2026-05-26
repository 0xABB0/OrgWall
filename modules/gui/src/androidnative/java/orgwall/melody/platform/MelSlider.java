package orgwall.melody.platform;

import android.content.Context;
import android.view.View;
import android.widget.SeekBar;

public final class MelSlider {

    private MelSlider() {}

    public static View create(int min, int max, int val) {
        return new MelSeekBar(MelGui.activity(), min, max, val);
    }

    public static void installChange(View v, long handle, long fn) {
        ((SeekBar) v).setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                if (fromUser) nativeChange(handle, fn, ((MelSeekBar) sb).melValue());
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch (SeekBar sb) {}
        });
    }

    public static native void nativeChange(long handle, long fn, int value);
}

/* A SeekBar that remembers its logical minimum so value() is in Melody's range,
 * not SeekBar's 0..(max-min) progress space. */
final class MelSeekBar extends SeekBar {

    private final int min;

    MelSeekBar(Context ctx, int min, int max, int val) {
        super(ctx);
        this.min = min;
        setMax(max - min);
        setProgress(val - min);
    }

    public int melValue()          { return getProgress() + min; }
    public void melSetValue(int v) { setProgress(v - min); }
}
