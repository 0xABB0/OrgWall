package orgwall.melody.platform;

import android.graphics.Color;
import android.view.View;
import android.widget.TextView;

public final class MelLabel {

    private MelLabel() {}

    public static View create(long handle, long parent, int x, int y, int w, int h, String text) {
        TextView v = new TextView(MelGui.activity());
        v.setText(text);
        v.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        MelGui.attach(v, parent, x, y, w, h);
        MelGui.registerView(handle, v);
        return v;
    }
}
