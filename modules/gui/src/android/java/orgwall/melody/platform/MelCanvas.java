package orgwall.melody.platform;

import android.view.View;

public final class MelCanvas {

    private MelCanvas() {}

    public static View create(long handle, long parent, int x, int y, int w, int h) {
        MelCanvasView v = new MelCanvasView(MelGui.activity(), handle);
        MelGui.attach(v, parent, x, y, w, h);
        MelGui.registerView(handle, v);
        return v;
    }
}
