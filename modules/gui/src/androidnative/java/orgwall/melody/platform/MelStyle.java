package orgwall.melody.platform;

import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.View;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/* Composable style appliers, one per styling primitive plus the
 * widget-specific ones. Each applies only the set fields; an unset field
 * leaves the platform default (or an earlier application) untouched. Colors
 * are ARGB ints. */
public final class MelStyle {

    private MelStyle() {}

    public static void applyText(View v, boolean hasFg, int fg, float sizeSp,
                                 int weight, boolean italic, String family) {
        TextView tv = (TextView) v;
        if (hasFg) tv.setTextColor(fg);
        if (sizeSp > 0) tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp);
        applyTypeface(tv, weight, italic, family);
    }

    public static void applySurface(View v, boolean hasBg, int bg,
                                    boolean hasBorder, int borderColor, float borderWidthDp,
                                    float radiusDp, int padL, int padT, int padR, int padB) {
        if (hasBg || hasBorder || radiusDp > 0) {
            GradientDrawable gd = new GradientDrawable();
            gd.setColor(hasBg ? bg : Color.TRANSPARENT);
            if (hasBorder) gd.setStroke(Math.max(1, px(borderWidthDp)), borderColor);
            if (radiusDp > 0) gd.setCornerRadius(px(radiusDp));
            v.setBackground(gd);
        }

        if (padL != 0 || padT != 0 || padR != 0 || padB != 0) {
            v.setPadding(padL, padT, padR, padB);
        }
    }

    public static void applySlider(View v, boolean hasTrack, int track,
                                   boolean hasThumb, int thumb) {
        SeekBar sb = (SeekBar) v;
        if (hasTrack) sb.setProgressTintList(ColorStateList.valueOf(track));
        if (hasThumb) sb.setThumbTintList(ColorStateList.valueOf(thumb));
    }

    public static void applyCheckTint(View v, int tint) {
        ((CompoundButton) v).setButtonTintList(ColorStateList.valueOf(tint));
    }

    public static void applyGroupBoxTitle(View v, boolean hasFg, int fg, float sizeSp,
                                          int weight, boolean italic, String family) {
        View title = ((LinearLayout) v).getChildAt(0);
        applyText(title, hasFg, fg, sizeSp, weight, italic, family);
    }

    public static void applySplitterDivider(View v, int color) {
        ((MelSplitter) v).setDividerColor(color);
    }

    private static int px(float dp) { return Math.round(dp * MelGui.density()); }

    private static void applyTypeface(TextView tv, int weight, boolean italic, String family) {
        boolean hasFamily = family != null && !family.isEmpty();
        if (weight == 0 && !italic && !hasFamily) return;

        Typeface base = hasFamily ? Typeface.create(family, Typeface.NORMAL) : tv.getTypeface();
        if (weight > 0 && Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            tv.setTypeface(Typeface.create(base, weight, italic));
            return;
        }
        /* Pre-28 (or weight unset): only the bold/italic axes exist. */
        int style = (weight >= 700 ? Typeface.BOLD : Typeface.NORMAL)
                  | (italic ? Typeface.ITALIC : Typeface.NORMAL);
        tv.setTypeface(Typeface.create(base, style));
    }
}
