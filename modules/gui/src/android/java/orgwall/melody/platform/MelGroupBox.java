package orgwall.melody.platform;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MelGroupBox {

    private MelGroupBox() {}

    public static View create(String title) {
        Context ctx = MelGui.activity();

        LinearLayout outer = new LinearLayout(ctx);
        outer.setOrientation(LinearLayout.VERTICAL);

        GradientDrawable border = new GradientDrawable();
        border.setColor(Color.TRANSPARENT);
        border.setStroke(MelGui.dp2px(1), Color.rgb(0x6A, 0x71, 0x80));
        border.setCornerRadius(MelGui.dp2px(4));
        outer.setBackground(border);

        int pad = MelGui.dp2px(8);
        outer.setPadding(pad, pad, pad, pad);

        TextView label = new TextView(ctx);
        label.setText(title);
        label.setTextColor(Color.rgb(0xE9, 0xEE, 0xF5));
        outer.addView(label, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        FrameLayout inner = new FrameLayout(ctx);
        outer.addView(inner, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        return outer;
    }

    public static View content(View outer) {
        return ((ViewGroup) outer).getChildAt(1);
    }
}
