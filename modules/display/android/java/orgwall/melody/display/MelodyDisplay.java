package orgwall.melody.display;

import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.util.DisplayMetrics;
import android.view.Display;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

public final class MelodyDisplay {
    private static Context context;
    private static DisplayManager manager;
    private static boolean inited;

    private MelodyDisplay() {}

    public static synchronized boolean ensureInited() {
        if (inited) return manager != null;
        inited = true;
        context = resolveAppContext();
        if (context == null) return false;
        manager = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
        return manager != null;
    }

    public static final class Info {
        public int     displayId;
        public String  name;
        public int     widthPx;
        public int     heightPx;
        public int     densityDpi;
        public int     state;
        public boolean wideGamut;
        public boolean hdr;
        public float   maxLuminance;
        public float   minLuminance;
        public float   maxAvgLuminance;
        public int[]   modeWidthPx;
        public int[]   modeHeightPx;
        public int[]   modeRefreshMhz;
    }

    public static Info[] enumerate() {
        if (!ensureInited()) return new Info[0];

        Display[] displays = manager.getDisplays();
        List<Info> out = new ArrayList<>();
        for (Display d : displays) {
            Info i = new Info();
            i.displayId = d.getDisplayId();
            i.name      = d.getName();

            Point real = new Point();
            d.getRealSize(real);
            i.widthPx  = real.x;
            i.heightPx = real.y;

            DisplayMetrics dm = new DisplayMetrics();
            d.getRealMetrics(dm);
            i.densityDpi = dm.densityDpi;

            i.state     = d.getState();
            i.wideGamut = d.isWideColorGamut();

            Display.HdrCapabilities hc = d.getHdrCapabilities();
            if (hc != null && hc.getSupportedHdrTypes().length > 0) {
                i.hdr             = true;
                i.maxLuminance    = hc.getDesiredMaxLuminance();
                i.minLuminance    = hc.getDesiredMinLuminance();
                i.maxAvgLuminance = hc.getDesiredMaxAverageLuminance();
            }

            Display.Mode[] modes = d.getSupportedModes();
            i.modeWidthPx    = new int[modes.length];
            i.modeHeightPx   = new int[modes.length];
            i.modeRefreshMhz = new int[modes.length];
            for (int m = 0; m < modes.length; m++) {
                i.modeWidthPx[m]    = modes[m].getPhysicalWidth();
                i.modeHeightPx[m]   = modes[m].getPhysicalHeight();
                i.modeRefreshMhz[m] = Math.round(modes[m].getRefreshRate() * 1000f);
            }
            out.add(i);
        }
        return out.toArray(new Info[0]);
    }

    private static Context resolveAppContext() {
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Method ca = at.getMethod("currentApplication");
            Object app = ca.invoke(null);
            if (app instanceof Context) return ((Context) app).getApplicationContext();
        } catch (ReflectiveOperationException ignored) {}
        return null;
    }
}
