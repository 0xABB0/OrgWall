package orgwall.melody.platform;

import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

import java.util.ArrayList;

public final class MelTabView extends LinearLayout {

    private final LinearLayout      strip;
    private final FrameLayout       content;
    private final ArrayList<View>   pages = new ArrayList<>();
    private long handle;
    private long onSelect;
    private int  selected = -1;

    private MelTabView() {
        super(MelGui.activity());
        setOrientation(VERTICAL);

        strip = new LinearLayout(MelGui.activity());
        strip.setOrientation(HORIZONTAL);
        addView(strip, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, MelGui.dp2px(48)));

        content = new FrameLayout(MelGui.activity());
        addView(content, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f));
    }

    public static View create(long handle, long onSelect) {
        MelTabView tv = new MelTabView();
        tv.handle   = handle;
        tv.onSelect = onSelect;
        return tv;
    }

    public static View addTab(View tabview, String title) {
        MelTabView tv = (MelTabView) tabview;
        final int index = tv.pages.size();

        Button b = new Button(MelGui.activity());
        b.setText(title);
        b.setAllCaps(false);
        b.setOnClickListener(v -> tv.select(index));
        tv.strip.addView(b, new LinearLayout.LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));

        FrameLayout page = new FrameLayout(MelGui.activity());
        page.setVisibility(tv.pages.isEmpty() ? View.VISIBLE : View.GONE);
        tv.content.addView(page, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        tv.pages.add(page);

        if (tv.selected < 0) tv.selected = 0;
        return page;
    }

    public static void select(View tabview, int index) {
        ((MelTabView) tabview).select(index);
    }

    public static int selected(View tabview) {
        return ((MelTabView) tabview).selected;
    }

    private void select(int index) {
        if (index < 0 || index >= pages.size()) return;
        for (int i = 0; i < pages.size(); i++) {
            pages.get(i).setVisibility(i == index ? View.VISIBLE : View.GONE);
        }
        selected = index;
        if (onSelect != 0) nativeSelect(handle, onSelect, index);
    }

    public static native void nativeSelect(long handle, long fn, int index);
}
