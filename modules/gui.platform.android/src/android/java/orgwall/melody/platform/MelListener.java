package orgwall.melody.platform;

import android.text.Editable;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.CompoundButton;
import android.widget.SeekBar;

public final class MelListener implements
        View.OnClickListener,
        View.OnFocusChangeListener,
        View.OnTouchListener,
        View.OnKeyListener,
        CompoundButton.OnCheckedChangeListener,
        SeekBar.OnSeekBarChangeListener,
        TextWatcher
{
    private final long handle;

    public MelListener(long handle) { this.handle = handle; }

    @Override public void onClick(View v) {
        NativeGuiHost.dispatchClick(handle);
    }

    @Override public void onFocusChange(View v, boolean focused) {
        NativeGuiHost.dispatchFocus(handle, focused);
    }

    @Override public boolean onTouch(View v, MotionEvent e) {
        int action = e.getActionMasked();
        int x = (int) e.getX();
        int y = (int) e.getY();
        return NativeGuiHost.dispatchPointer(handle, action, x, y);
    }

    @Override public boolean onKey(View v, int keyCode, KeyEvent e) {
        boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        return NativeGuiHost.dispatchKey(handle, keyCode, down);
    }

    @Override public void onCheckedChanged(CompoundButton b, boolean checked) {
        NativeGuiHost.dispatchValueChanged(handle, checked ? 1 : 0);
    }

    @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
        NativeGuiHost.dispatchValueChanged(handle, progress);
    }

    @Override public void onStartTrackingTouch(SeekBar s) {}
    @Override public void onStopTrackingTouch(SeekBar s) {}

    @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
    @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
        NativeGuiHost.dispatchTextChanged(handle, s.toString());
    }
    @Override public void afterTextChanged(Editable s) {}
}
