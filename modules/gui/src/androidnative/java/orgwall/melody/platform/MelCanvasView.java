package orgwall.melody.platform;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

public final class MelCanvasView extends View {

    private final long handle;
    private final long fnPaint, fnDown, fnMove, fnUp, fnKeyDown, fnKeyUp;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

    public MelCanvasView(Context ctx, long handle, long fnPaint,
                         long fnDown, long fnMove, long fnUp, long fnKeyDown, long fnKeyUp) {
        super(ctx);
        this.handle    = handle;
        this.fnPaint   = fnPaint;
        this.fnDown    = fnDown;
        this.fnMove    = fnMove;
        this.fnUp      = fnUp;
        this.fnKeyDown = fnKeyDown;
        this.fnKeyUp   = fnKeyUp;
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (fnPaint == 0) return;
        float d = MelGui.density();
        int save = canvas.save();
        canvas.scale(d, d);
        nativePaint(handle, fnPaint, canvas, paint,
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
                if (fnDown != 0) nativePointer(handle, fnDown, x, y);
                return true;
            case MotionEvent.ACTION_MOVE:
                if (fnMove != 0) nativePointer(handle, fnMove, x, y);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (fnUp != 0) nativePointer(handle, fnUp, x, y);
                return true;
            default:
                return super.onTouchEvent(ev);
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (fnKeyDown != 0) nativeKey(handle, fnKeyDown, keyCode);
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (fnKeyUp != 0) nativeKey(handle, fnKeyUp, keyCode);
        return super.onKeyUp(keyCode, event);
    }

    public static native void nativePaint  (long handle, long fn, Canvas canvas, Paint paint, int w, int h);
    public static native void nativePointer(long handle, long fn, int x, int y);
    public static native void nativeKey    (long handle, long fn, int key);
}
