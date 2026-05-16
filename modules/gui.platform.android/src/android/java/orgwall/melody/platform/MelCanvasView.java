package orgwall.melody.platform;

import android.content.Context;
import android.graphics.Canvas;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

public final class MelCanvasView extends View {
    private final long handle;

    public MelCanvasView(Context context, long handle) {
        super(context);
        this.handle = handle;
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        NativeGuiHost.dispatchPaint(handle, canvas);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int x = (int) e.getX();
        int y = (int) e.getY();
        if (action == MotionEvent.ACTION_DOWN) requestFocus();
        NativeGuiHost.dispatchPointer(handle, action, x, y);
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent e) {
        if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyDown(keyCode, e);
        NativeGuiHost.dispatchKey(handle, keyCode, true);
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent e) {
        if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyUp(keyCode, e);
        NativeGuiHost.dispatchKey(handle, keyCode, false);
        return true;
    }
}
