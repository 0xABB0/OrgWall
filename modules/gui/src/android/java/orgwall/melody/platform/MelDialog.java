package orgwall.melody.platform;

import android.app.Dialog;
import android.view.View;
import android.view.Window;
import android.widget.FrameLayout;

import java.util.HashMap;

public final class MelDialog {

    private MelDialog() {}

    private static final class Entry {
        final Dialog dialog;
        final long   fn;
        Entry(Dialog d, long f) { dialog = d; fn = f; }
    }

    private static final HashMap<Long, Entry> dialogs = new HashMap<>();

    public static View create(String title, long handle, long onResultFn) {
        Dialog d = new Dialog(MelGui.activity());
        Window w = d.getWindow();
        if (w != null) w.requestFeature(Window.FEATURE_NO_TITLE);
        d.setTitle(title);

        FrameLayout content = new FrameLayout(MelGui.activity());
        d.setContentView(content);
        d.setCancelable(true);
        d.setOnCancelListener(dlg -> result(handle, 0));

        dialogs.put(handle, new Entry(d, onResultFn));
        d.show();
        return content;
    }

    public static void close(long handle, int res) {
        result(handle, res);
    }

    private static void result(long handle, int res) {
        Entry e = dialogs.remove(handle);
        if (e == null) return;
        if (e.dialog.isShowing()) e.dialog.dismiss();
        nativeResult(handle, e.fn, res);
    }

    public static native void nativeResult(long handle, long fn, int result);
}
