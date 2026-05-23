package orgwall.melody.platform;

import android.content.Context;
import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.View;

public final class MelCanvasView extends View {

    private final long handle;

    public MelCanvasView(Context ctx, long handle) {
        super(ctx);
        this.handle = handle;
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        MelGui.nativeFireCanvasPaint(handle, canvas, getWidth(), getHeight());
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        int x = (int) ev.getX();
        int y = (int) ev.getY();
        switch (ev.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                requestFocus();
                MelGui.nativeFirePointer(handle, 0, x, y);
                return true;
            case MotionEvent.ACTION_MOVE:
                MelGui.nativeFirePointer(handle, 1, x, y);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                MelGui.nativeFirePointer(handle, 2, x, y);
                return true;
            default:
                return super.onTouchEvent(ev);
        }
    }
}
