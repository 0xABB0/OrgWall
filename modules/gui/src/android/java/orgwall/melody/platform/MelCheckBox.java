package orgwall.melody.platform;

import android.graphics.Color;
import android.view.View;
import android.widget.CheckBox;
import android.widget.CompoundButton;

public final class MelCheckBox {

    private MelCheckBox() {}

    public static View create(long handle, long parent, int x, int y, int w, int h,
                              String text, boolean checked) {
        CheckBox v = new CheckBox(MelGui.activity());
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        v.setChecked(checked);
        v.setOnCheckedChangeListener((CompoundButton btn, boolean isChecked)
                -> nativeToggled(handle, isChecked));
        v.setOnFocusChangeListener((view, hasFocus) -> MelGui.nativeFireFocus(handle, hasFocus));
        MelGui.attach(v, parent, x, y, w, h);
        MelGui.registerView(handle, v);
        return v;
    }

    public static boolean isChecked(long handle) {
        View v = MelGui.viewFor(handle);
        return v instanceof CheckBox && ((CheckBox) v).isChecked();
    }

    public static native void nativeToggled(long handle, boolean checked);
}
