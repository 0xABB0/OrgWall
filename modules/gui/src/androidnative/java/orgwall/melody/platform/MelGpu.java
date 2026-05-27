package orgwall.melody.platform;

import android.view.View;

public final class MelGpu {

    private MelGpu() {}

    public static View create(long handle, long fnResize,
                              long fnDown, long fnMove, long fnUp,
                              long fnKeyDown, long fnKeyUp) {
        return new MelGpuView(MelGui.activity(), handle,
                fnResize, fnDown, fnMove, fnUp, fnKeyDown, fnKeyUp);
    }
}
