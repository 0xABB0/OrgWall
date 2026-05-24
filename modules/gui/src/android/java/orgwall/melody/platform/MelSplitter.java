package orgwall.melody.platform;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import java.util.ArrayList;

public final class MelSplitter extends ViewGroup {

    private final boolean vertical;
    private final int     divider;
    private long handle;

    private final ArrayList<View>    panes = new ArrayList<>();
    private final ArrayList<Integer> sizes = new ArrayList<>();
    private final ArrayList<Integer> mins  = new ArrayList<>();

    private final Paint dividerPaint = new Paint();

    private int dragIndex = -1;
    private int dragStart;
    private int dragBaseLeft;

    private MelSplitter(boolean v) {
        super(MelGui.activity());
        vertical = v;
        divider  = MelGui.dp2px(8);
        dividerPaint.setColor(Color.rgb(0x6A, 0x71, 0x80));
        setWillNotDraw(false);
    }

    public static View create(long handle, boolean vertical) {
        MelSplitter s = new MelSplitter(vertical);
        s.handle = handle;
        return s;
    }

    public static View addPane(View splitter, int minDp, int initialDp) {
        MelSplitter s = (MelSplitter) splitter;
        FrameLayout pane = new FrameLayout(MelGui.activity());
        s.addView(pane);
        s.panes.add(pane);
        s.sizes.add(initialDp > 0 ? MelGui.dp2px(initialDp) : 0);
        s.mins.add(minDp > 0 ? MelGui.dp2px(minDp) : 0);
        return pane;
    }

    private void resolve(int avail) {
        int n = sizes.size();
        if (n == 0) return;
        int usable = Math.max(0, avail - divider * (n - 1));

        int fixed = 0, zeros = 0;
        for (int i = 0; i < n; i++) {
            int s = sizes.get(i);
            if (s > 0) fixed += s; else zeros++;
        }
        if (zeros > 0) {
            int each = Math.max(1, Math.max(0, usable - fixed) / zeros);
            for (int i = 0; i < n; i++) if (sizes.get(i) <= 0) sizes.set(i, each);
        }

        int sum = 0;
        for (int s : sizes) sum += s;
        if (sum <= 0) sum = 1;

        int used = 0;
        for (int i = 0; i < n; i++) {
            int ext = (i == n - 1) ? (usable - used) : (sizes.get(i) * usable / sum);
            if (ext < 0) ext = 0;
            sizes.set(i, ext);
            used += ext;
        }
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int w = MeasureSpec.getSize(widthSpec);
        int h = MeasureSpec.getSize(heightSpec);
        resolve(vertical ? h : w);

        int cross = vertical ? w : h;
        for (int i = 0; i < panes.size(); i++) {
            int ext = sizes.get(i);
            int pw  = vertical ? cross : ext;
            int ph  = vertical ? ext   : cross;
            panes.get(i).measure(
                MeasureSpec.makeMeasureSpec(pw, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(ph, MeasureSpec.EXACTLY));
        }
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int cross = vertical ? (r - l) : (b - t);
        int used  = 0;
        for (int i = 0; i < panes.size(); i++) {
            int ext = sizes.get(i);
            if (vertical) panes.get(i).layout(0, used, cross, used + ext);
            else          panes.get(i).layout(used, 0, used + ext, cross);
            used += ext + divider;
        }
        nativeLayout(handle);
    }

    @Override
    protected void onDraw(Canvas c) {
        int cross = vertical ? getWidth() : getHeight();
        int used  = 0;
        for (int i = 0; i < panes.size() - 1; i++) {
            used += sizes.get(i);
            if (vertical) c.drawRect(0, used, cross, used + divider, dividerPaint);
            else          c.drawRect(used, 0, used + divider, cross, dividerPaint);
            used += divider;
        }
    }

    private int hitDivider(int pos) {
        int used = 0;
        for (int i = 0; i < panes.size() - 1; i++) {
            used += sizes.get(i);
            if (pos >= used && pos < used + divider) return i;
            used += divider;
        }
        return -1;
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent e) {
        if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
            int pos = (int) (vertical ? e.getY() : e.getX());
            if (hitDivider(pos) >= 0) return true;
        }
        return false;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int pos = (int) (vertical ? e.getY() : e.getX());
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: {
                int d = hitDivider(pos);
                if (d < 0) return false;
                dragIndex = d;
                dragStart = pos;
                dragBaseLeft = sizes.get(d);
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (dragIndex < 0) return false;
                int d = dragIndex;
                int combined = sizes.get(d) + sizes.get(d + 1);
                int newLeft = dragBaseLeft + (pos - dragStart);
                int minL = mins.get(d);
                int minR = mins.get(d + 1);
                if (newLeft < minL) newLeft = minL;
                if (newLeft > combined - minR) newLeft = combined - minR;
                sizes.set(d, newLeft);
                sizes.set(d + 1, combined - newLeft);
                requestLayout();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                dragIndex = -1;
                return true;
        }
        return false;
    }

    public static native void nativeLayout(long handle);
}
