package orgwall.melody.platform;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.widget.FrameLayout;

public final class MelodyActivity extends Activity {

    static {
        System.loadLibrary("melody");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(0x1A, 0x22, 0x30));
        setContentView(root);

        MelGui.start(this, root);
    }

    @Override
    protected void onDestroy() {
        MelGui.stop();
        super.onDestroy();
    }
}
