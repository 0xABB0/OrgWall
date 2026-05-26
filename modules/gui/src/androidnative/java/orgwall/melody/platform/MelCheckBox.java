package orgwall.melody.platform;

import android.graphics.Color;
import android.view.View;
import android.widget.CheckBox;
import android.widget.CompoundButton;

public final class MelCheckBox {

    private MelCheckBox() {}

    public static View create(String text, boolean checked) {
        CheckBox v = new CheckBox(MelGui.activity());
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        v.setChecked(checked);
        return v;
    }

    public static void installToggle(View v, long handle, long fn) {
        ((CompoundButton) v).setOnCheckedChangeListener(
                (CompoundButton btn, boolean isChecked) -> nativeToggle(handle, fn, isChecked));
    }

    public static native void nativeToggle(long handle, long fn, boolean checked);
}
