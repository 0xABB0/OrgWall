package orgwall.melody.platform;

import android.content.Context;
import android.graphics.Canvas;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

public final class MelCanvasView extends View {

    private final long handle;

    public MelCanvasView(Context ctx, long handle) {
        super(ctx);
        this.handle = handle;
        setFocusable(true);
        setFocusableInTouchMode(true);
        setOnFocusChangeListener((view, hasFocus) -> MelGui.nativeFireFocus(handle, hasFocus));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float d = MelGui.density();
        int save = canvas.save();
        canvas.scale(d, d);
        nativePaint(handle, canvas,
                Math.round(getWidth()  / d),
                Math.round(getHeight() / d));
        canvas.restoreToCount(save);
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        float d = MelGui.density();
        int x = Math.round(ev.getX() / d);
        int y = Math.round(ev.getY() / d);
        switch (ev.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                requestFocus();
                nativePointer(handle, 0, x, y);
                return true;
            case MotionEvent.ACTION_MOVE:
                nativePointer(handle, 1, x, y);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                nativePointer(handle, 2, x, y);
                return true;
            default:
                return super.onTouchEvent(ev);
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        nativeKey(handle, keyCode, true);
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        nativeKey(handle, keyCode, false);
        return super.onKeyUp(keyCode, event);
    }

    public static native void nativePaint  (long handle, Canvas canvas, int w, int h);
    public static native void nativePointer(long handle, int kind, int x, int y);
    public static native void nativeKey    (long handle, int key, boolean down);
}
