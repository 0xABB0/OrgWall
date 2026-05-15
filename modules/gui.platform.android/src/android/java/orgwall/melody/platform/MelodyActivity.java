package orgwall.melody.platform;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowInsets;
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
    private static native void nativeAppResume();
    private static native void nativeAppPause();
    private static native void nativeAppDestroy();
    private static native void nativeAppBack();
    private static native void nativeAppConfigChanged();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        activityName = resolveActivityName();

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(21, 31, 42));
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            int l, t, r, b;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                int mask = WindowInsets.Type.systemBars() | WindowInsets.Type.ime();
                Insets in = insets.getInsets(mask);
                l = in.left; t = in.top; r = in.right; b = in.bottom;
            } else {
                l = insets.getSystemWindowInsetLeft();
                t = insets.getSystemWindowInsetTop();
                r = insets.getSystemWindowInsetRight();
                b = insets.getSystemWindowInsetBottom();
            }
            v.setPadding(l, t, r, b);
            return insets;
        });
        setContentView(root);

        host = new NativeGuiHost(this, root);
        nativeBuildActivity(host, activityName);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (host != null) nativeAppResume();
    }

    @Override
    protected void onPause() {
        if (host != null) nativeAppPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (host != null) nativeAppDestroy();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (host != null) nativeAppBack();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (host != null) nativeAppConfigChanged();
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

}
