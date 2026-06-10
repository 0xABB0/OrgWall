package orgwall.melody.platform;

import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.View;
import android.widget.SeekBar;
import android.widget.TextView;

public final class MelStyle {

    private MelStyle() {}

    /* Apply only the set fields; an unset field leaves the platform default
     * (or an earlier application) untouched. Colors are ARGB ints. */
    public static void apply(View v, boolean hasFg, int fg, boolean hasBg, int bg,
                             boolean hasBorder, int borderColor, float borderWidthDp,
                             float radiusDp, float textSizeSp, int fontWeight, boolean italic,
                             String family, int padL, int padT, int padR, int padB) {
        if (v instanceof TextView) {
            TextView tv = (TextView) v;
            if (hasFg) tv.setTextColor(fg);
            if (textSizeSp > 0) tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, textSizeSp);
            applyTypeface(tv, fontWeight, italic, family);
        } else if (hasFg && v instanceof SeekBar) {
            ColorStateList tint = ColorStateList.valueOf(fg);
            SeekBar sb = (SeekBar) v;
            sb.setThumbTintList(tint);
            sb.setProgressTintList(tint);
        }

        if (hasBg || hasBorder || radiusDp > 0) {
            GradientDrawable gd = new GradientDrawable();
            gd.setColor(hasBg ? bg : Color.TRANSPARENT);
            if (hasBorder) gd.setStroke(Math.max(1, px(borderWidthDp)), borderColor);
            if (radiusDp > 0) gd.setCornerRadius(px(radiusDp));
            v.setBackground(gd);
        }

        if (padL != 0 || padT != 0 || padR != 0 || padB != 0) {
            v.setPadding(MelGui.dp2px(padL), MelGui.dp2px(padT),
                         MelGui.dp2px(padR), MelGui.dp2px(padB));
        }
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
