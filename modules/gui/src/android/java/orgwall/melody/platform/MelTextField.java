package orgwall.melody.platform;

import android.graphics.Color;
import android.view.View;

public final class MelTextField {

    private MelTextField() {}

    public static View create(long handle, String text, long fnTextChanged) {
        MelEditText v = new MelEditText(MelGui.activity(), handle, fnTextChanged);
        v.setSingleLine(true);
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        return v;
    }

    public static native void nativeTextChanged(long handle, long fn, String text);
}
