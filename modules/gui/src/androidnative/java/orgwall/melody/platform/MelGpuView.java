package orgwall.melody.platform;

import android.content.Context;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

// A GPU-rendered surface. The GPU backend (Vulkan) renders into the Surface's
// ANativeWindow. The surface is created asynchronously, so the native side is
// notified through surfaceChanged/surfaceDestroyed rather than at construction.
public final class MelGpuView extends SurfaceView implements SurfaceHolder.Callback {

    private final long handle;
    private final long fnResize, fnDown, fnMove, fnUp, fnKeyDown, fnKeyUp;

    public MelGpuView(Context ctx, long handle, long fnResize,
                      long fnDown, long fnMove, long fnUp, long fnKeyDown, long fnKeyUp) {
        super(ctx);
        this.handle    = handle;
        this.fnResize  = fnResize;
        this.fnDown    = fnDown;
        this.fnMove    = fnMove;
        this.fnUp      = fnUp;
        this.fnKeyDown = fnKeyDown;
        this.fnKeyUp   = fnKeyUp;
        setFocusable(true);
        setFocusableInTouchMode(true);
        // A SurfaceView's surface is composited behind its window by default; put
        // it on top so the GPU content is not hidden by an opaque parent.
        setZOrderOnTop(true);
        getHolder().addCallback(this);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) { }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeSurfaceChanged(handle, fnResize, holder.getSurface(), width, height);
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSurfaceDestroyed(handle, fnResize);
    }

    @Override public boolean onTouchEvent(MotionEvent ev) {
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

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (fnKeyDown != 0) nativeKey(handle, fnKeyDown, keyCode);
        return super.onKeyDown(keyCode, event);
    }

    @Override public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (fnKeyUp != 0) nativeKey(handle, fnKeyUp, keyCode);
        return super.onKeyUp(keyCode, event);
    }

    public static native void nativeSurfaceChanged(long handle, long fnResize, Surface surface, int w, int h);
    public static native void nativeSurfaceDestroyed(long handle, long fnResize);
    public static native void nativePointer(long handle, long fn, int x, int y);
    public static native void nativeKey(long handle, long fn, int key);
}
