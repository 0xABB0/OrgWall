package orgwall.melody.platform;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Bundle;
import android.widget.FrameLayout;

public final class MelodyActivity extends Activity {
    public static final String EXTRA_ACTIVITY_NAME = "melody.activity_name";
    public static final String META_ACTIVITY_NAME = "melody.activity_name";
    private static final String DEFAULT_ACTIVITY_NAME = "main";

    static {
        System.loadLibrary("melody");
    }

    private NativeGuiHost host;
    private String activityName;

    private static native void nativeBuildActivity(NativeGuiHost host, String activityName);
    private static native void nativeResumeActivity(NativeGuiHost host, String activityName);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        activityName = resolveActivityName();

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(21, 31, 42));
        root.setPadding(0, statusBarHeight(), 0, 0);
        setContentView(root);

        host = new NativeGuiHost(this, root);
        nativeBuildActivity(host, activityName);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (host != null) {
            nativeResumeActivity(host, activityName);
        }
    }

    private String resolveActivityName() {
        String fromExtra = getIntent() != null ? getIntent().getStringExtra(EXTRA_ACTIVITY_NAME) : null;
        if (fromExtra != null && !fromExtra.isEmpty()) return fromExtra;

        try {
            ActivityInfo info = getPackageManager().getActivityInfo(
                    getComponentName(), PackageManager.GET_META_DATA);
            if (info.metaData != null) {
                String fromMeta = info.metaData.getString(META_ACTIVITY_NAME);
                if (fromMeta != null && !fromMeta.isEmpty()) return fromMeta;
            }
        } catch (PackageManager.NameNotFoundException ignored) {
        }

        return DEFAULT_ACTIVITY_NAME;
    }

    private int statusBarHeight() {
        int id = getResources().getIdentifier("status_bar_height", "dimen", "android");
        return id > 0 ? getResources().getDimensionPixelSize(id) : 0;
    }
}
