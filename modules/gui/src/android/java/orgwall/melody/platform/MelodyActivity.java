package orgwall.melody.platform;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowInsets;
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
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
                return WindowInsets.CONSUMED;
            }
            return applyLegacyInsets(v, insets);
        });
        setContentView(root);

        MelGui.setBackHost(this);
        MelGui.start(this, root);
    }

    @SuppressWarnings("deprecation")
    private static WindowInsets applyLegacyInsets(android.view.View v, WindowInsets insets) {
        v.setPadding(insets.getSystemWindowInsetLeft(),
                     insets.getSystemWindowInsetTop(),
                     insets.getSystemWindowInsetRight(),
                     insets.getSystemWindowInsetBottom());
        return insets.consumeSystemWindowInsets();
    }

    /* The OS back gesture only reaches us while there is in-app history to pop;
     * at the root the callback is unregistered, so the system performs its own
     * default back (exit / predictive-back-to-home). */
    @Override
    public void onBackAvailable(boolean canGoBack) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;
        OnBackInvokedDispatcher d = getOnBackInvokedDispatcher();
        if (canGoBack && backCallback == null) {
            backCallback = () -> MelGui.back();
            d.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        } else if (!canGoBack && backCallback != null) {
            d.unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (!MelGui.back()) super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        if (backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        MelGui.stop();
        super.onDestroy();
    }
}
