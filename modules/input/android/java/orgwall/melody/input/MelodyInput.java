package orgwall.melody.input;

import android.view.KeyEvent;
import android.view.MotionEvent;

public final class MelodyInput {
    private MelodyInput() {}

    public static native void nativeKey(int action, int keyCode, int metaState, int unicode, boolean repeat);

    public static native void nativeMotion(int source, int action, int pointerId, float x, float y, float pressure);

    public static boolean dispatchKey(KeyEvent event) {
        int unicode = event.getUnicodeChar(event.getMetaState());
        nativeKey(event.getAction(), event.getKeyCode(), event.getMetaState(), unicode, event.getRepeatCount() > 0);
        return true;
    }

    public static boolean dispatchMotion(MotionEvent event) {
        int source = event.getSource();
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        int pointerId = event.getPointerId(index);
        nativeMotion(source, action, pointerId, event.getX(index), event.getY(index), event.getPressure(index));
        return true;
    }
}
