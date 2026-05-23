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
        float d = MelGui.density();
        int save = canvas.save();
        canvas.scale(d, d);
        MelGui.nativeFireCanvasPaint(handle, canvas,
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
