package orgwall.melody.platform;

import android.view.View;
import android.widget.Button;

public final class MelButton {

    private MelButton() {}

    public static View create(String text) {
        Button v = new Button(MelGui.activity());
        v.setText(text);
        v.setAllCaps(false);
        return v;
    }

    public static void installClick(View v, long handle, long fn) {
        v.setOnClickListener(view -> nativeClick(handle, fn));
    }

    public static native void nativeClick(long handle, long fn);
}
