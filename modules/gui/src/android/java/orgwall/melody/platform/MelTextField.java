package orgwall.melody.platform;

import android.graphics.Color;
import android.view.View;

public final class MelTextField {

    private MelTextField() {}

    public static View create(long handle, long parent, int x, int y, int w, int h, String text) {
        MelEditText v = new MelEditText(MelGui.activity(), handle);
        v.setSingleLine(true);
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        v.setOnFocusChangeListener((view, hasFocus) -> MelGui.nativeFireFocus(handle, hasFocus));
        MelGui.attach(v, parent, x, y, w, h);
        MelGui.registerView(handle, v);
        return v;
    }

    public static native void nativeTextChanged(long handle, String text);
}
