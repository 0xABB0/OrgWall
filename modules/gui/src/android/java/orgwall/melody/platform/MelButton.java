package orgwall.melody.platform;

import android.view.View;
import android.widget.Button;

public final class MelButton {

    private MelButton() {}

    public static View create(long handle, long parent, int x, int y, int w, int h, String text) {
        Button v = new Button(MelGui.activity());
        v.setText(text);
        v.setAllCaps(false);
        v.setOnClickListener(view -> nativeClicked(handle));
        v.setOnFocusChangeListener((view, hasFocus) -> MelGui.nativeFireFocus(handle, hasFocus));
        MelGui.attach(v, parent, x, y, w, h);
        MelGui.registerView(handle, v);
        return v;
    }

    public static native void nativeClicked(long handle);
}
