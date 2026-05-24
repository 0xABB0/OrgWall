package orgwall.melody.platform;

import android.view.View;
import android.widget.FrameLayout;

public final class MelPanel {

    private MelPanel() {}

    public static View create() {
        return new FrameLayout(MelGui.activity());
    }
}
