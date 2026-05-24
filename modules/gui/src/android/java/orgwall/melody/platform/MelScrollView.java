package orgwall.melody.platform;

import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ScrollView;

public final class MelScrollView {

    private MelScrollView() {}

    public static View create(int contentWpx, int contentHpx) {
        ScrollView sv = new ScrollView(MelGui.activity());
        sv.setFillViewport(true);

        FrameLayout inner = new FrameLayout(MelGui.activity());
        sv.addView(inner, new FrameLayout.LayoutParams(
                contentWpx > 0 ? contentWpx : ViewGroup.LayoutParams.MATCH_PARENT,
                contentHpx > 0 ? contentHpx : ViewGroup.LayoutParams.MATCH_PARENT));

        return sv;
    }

    public static View content(View sv) {
        return ((ViewGroup) sv).getChildAt(0);
    }
}
