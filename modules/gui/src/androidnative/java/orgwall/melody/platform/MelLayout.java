package orgwall.melody.platform;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

public final class MelLayout {

    private MelLayout() {}

    /* Mel_Align values as the C layout module defines them. */
    private static final int ALIGN_START   = 1;
    private static final int ALIGN_CENTER  = 2;
    private static final int ALIGN_END     = 3;
    private static final int ALIGN_STRETCH = 4;

    /* Lower a linear layout: build a LinearLayout inside `host`, move host's
     * existing children into it (order kept; C re-issues their params from the
     * layoutable data right after), and return it as the new child container.
     * Spacing is a transparent middle divider so it stays a native concern. */
    public static View lower(View host, boolean vertical, int spacingPx, int marginPx, int crossAlign) {
        ViewGroup hostGroup = (ViewGroup) host;

        LinearLayout ll = new LinearLayout(MelGui.activity());
        ll.setOrientation(vertical ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        ll.setPadding(marginPx, marginPx, marginPx, marginPx);

        if (spacingPx > 0) {
            GradientDrawable divider = new GradientDrawable();
            divider.setColor(Color.TRANSPARENT);
            divider.setSize(spacingPx, spacingPx);
            ll.setDividerDrawable(divider);
            ll.setShowDividers(LinearLayout.SHOW_DIVIDER_MIDDLE);
        }

        switch (crossAlign) {
            case ALIGN_CENTER: ll.setGravity(vertical ? Gravity.CENTER_HORIZONTAL : Gravity.CENTER_VERTICAL); break;
            case ALIGN_END:    ll.setGravity(vertical ? Gravity.RIGHT : Gravity.BOTTOM); break;
            case ALIGN_START:
            case ALIGN_STRETCH:
            default:           break;
        }

        while (hostGroup.getChildCount() > 0) {
            View c = hostGroup.getChildAt(0);
            hostGroup.removeViewAt(0);
            ll.addView(c, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        /* Every lowering host in this backend is a FrameLayout (panel, frame,
         * groupbox/scroll content, dialog content, tab page, split pane) —
         * except a host that was lowered before, which is a LinearLayout. */
        ViewGroup.LayoutParams fill = (hostGroup instanceof LinearLayout)
                ? new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT)
                : new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT);
        hostGroup.addView(ll, fill);
        return ll;
    }
}
