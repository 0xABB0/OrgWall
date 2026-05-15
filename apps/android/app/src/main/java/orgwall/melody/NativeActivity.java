package orgwall.melody;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.widget.FrameLayout;

public class NativeActivity extends Activity {
    static {
        System.loadLibrary("melody_android");
    }

    private NativeGuiHost host;

    private static native void nativeBuildActivity(NativeGuiHost host, String activityName);
    private static native void nativeResumeActivity(NativeGuiHost host, String activityName);

    protected String activityName() {
        return "main";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(21, 31, 42));
        root.setPadding(0, statusBarHeight(), 0, 0);
        setContentView(root);

        host = new NativeGuiHost(this, root);
        nativeBuildActivity(host, activityName());
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (host != null) {
            nativeResumeActivity(host, activityName());
        }
    }

    private int statusBarHeight() {
        int id = getResources().getIdentifier("status_bar_height", "dimen", "android");
        return id > 0 ? getResources().getDimensionPixelSize(id) : 0;
    }
}
