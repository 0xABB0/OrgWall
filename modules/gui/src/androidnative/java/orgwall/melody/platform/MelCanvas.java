package orgwall.melody.platform;

import android.view.View;

public final class MelCanvas {

    private MelCanvas() {}

    public static View create(long handle, long fnPaint,
                              long fnDown, long fnMove, long fnUp,
                              long fnKeyDown, long fnKeyUp) {
        return new MelCanvasView(MelGui.activity(), handle,
                fnPaint, fnDown, fnMove, fnUp, fnKeyDown, fnKeyUp);
    }
}
