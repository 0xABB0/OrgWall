package orgwall.melody.platform;

import android.content.Context;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.EditText;

public final class MelEditText extends EditText {

    private final long handle;
    private final long fnTextChanged;
    private boolean suppress;

    public MelEditText(Context ctx, long handle, long fnTextChanged) {
        super(ctx);
        this.handle        = handle;
        this.fnTextChanged = fnTextChanged;
        if (fnTextChanged != 0) {
            addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence s, int st, int c, int a) {}
                @Override public void onTextChanged    (CharSequence s, int st, int b, int c) {}
                @Override public void afterTextChanged (Editable s) {
                    if (!suppress) MelTextField.nativeTextChanged(handle, fnTextChanged, s.toString());
                }
            });
        }
    }

    @Override
    public void setText(CharSequence text, BufferType type) {
        suppress = true;
        super.setText(text, type);
        suppress = false;
    }
}
