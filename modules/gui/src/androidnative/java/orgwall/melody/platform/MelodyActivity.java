package orgwall.melody.platform;

import android.app.Activity;
import android.content.ComponentCallbacks2;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.widget.FrameLayout;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

public final class MelodyActivity extends Activity implements MelGui.BackHost {

    static {
        System.loadLibrary("melody");
    }

    private OnBackInvokedCallback backCallback;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(0x1A, 0x22, 0x30));
        setContentView(root);

        MelGui.setBackHost(this);
        MelGui.start(this, root);
    }

    /* The OS back gesture only reaches us while there is in-app history to pop;
     * at the root the callback is unregistered, so the system performs its own
     * default back (exit / predictive-back-to-home). */
    @Override
    public void onBackAvailable(boolean canGoBack) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;
        OnBackInvokedDispatcher d = getOnBackInvokedDispatcher();
        if (canGoBack && backCallback == null) {
            backCallback = () -> MelGui.nativeOsBack();
            d.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        } else if (!canGoBack && backCallback != null) {
            d.unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (!MelGui.nativeOsBack()) super.onBackPressed();
    }

    @Override
    protected void onResume() {
        super.onResume();
        MelGui.nativeOnResume();
    }

    @Override
    protected void onPause() {
        MelGui.nativeOnPause();
        super.onPause();
    }

    @Override
    protected void onStop() {
        MelGui.nativeOnStop();
        super.onStop();
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW) MelGui.nativeOnLowMemory();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onLowMemory() {
        super.onLowMemory();
        MelGui.nativeOnLowMemory();
    }

    @Override
    protected void onDestroy() {
        if (backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        MelGui.nativeOnDestroy();
        MelGui.stop();
        super.onDestroy();
    }
}
