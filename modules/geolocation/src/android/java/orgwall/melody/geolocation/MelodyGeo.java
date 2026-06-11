package orgwall.melody.geolocation;

import android.annotation.SuppressLint;
import android.app.Application;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Address;
import android.location.Geocoder;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;

import java.io.IOException;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@SuppressLint("MissingPermission")
public final class MelodyGeo {
    static final int VALID_POSITION = 1;
    static final int VALID_ALTITUDE = 1 << 1;
    static final int VALID_HACC = 1 << 2;
    static final int VALID_VACC = 1 << 3;
    static final int VALID_SPEED = 1 << 4;
    static final int VALID_SPEED_ACC = 1 << 5;
    static final int VALID_COURSE = 1 << 6;
    static final int VALID_COURSE_ACC = 1 << 7;
    static final int VALID_UTC = 1 << 8;

    static final int RESULT_OK = 0;
    static final int RESULT_DENIED = 1;
    static final int RESULT_UNAVAILABLE = 2;
    static final int RESULT_LOST = 3;
    static final int RESULT_EXHAUSTED = 7;

    static native void nativeOnFix(boolean stream, long reqPtr, double lat, double lon, double alt,
            double hacc, double vacc, double speed, double speedAcc, double bearing, double bearingAcc,
            long utcMs, int valid);
    static native void nativeOnStreamResult(int code);
    static native void nativeOnRequestFailed(long reqPtr, int code);
    static native void nativeOnRegion(long regionPtr, boolean entered);
    static native void nativeOnRegionResult(long regionPtr, int code);
    static native void nativeOnGeocode(long geocodePtr, int code, int count, String[] strings, double[] coords);

    private static LocationListener streamListener;
    private static final Map<Long, Object> singles = new HashMap<>();
    private static Handler geocodeHandler;

    private MelodyGeo() {}

    static Context context() {
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            Method cur = at.getMethod("currentApplication");
            Object app = cur.invoke(null);
            return app instanceof Application ? (Application) app : null;
        } catch (Throwable t) {
            return null;
        }
    }

    private static LocationManager manager() {
        Context ctx = context();
        return ctx == null ? null : (LocationManager) ctx.getSystemService(Context.LOCATION_SERVICE);
    }

    public static boolean granted() {
        Context ctx = context();
        return ctx != null
                && ctx.checkSelfPermission("android.permission.ACCESS_FINE_LOCATION") == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean backgroundGranted() {
        if (Build.VERSION.SDK_INT < 29) return granted();
        Context ctx = context();
        return ctx != null
                && ctx.checkSelfPermission("android.permission.ACCESS_BACKGROUND_LOCATION") == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean geocoderPresent() {
        return Geocoder.isPresent();
    }

    static void deliver(boolean stream, long reqPtr, Location l) {
        int valid = VALID_POSITION | VALID_UTC;
        double hacc = 0, vacc = 0, alt = 0, speed = 0, speedAcc = 0, bearing = 0, bearingAcc = 0;
        if (l.hasAccuracy()) { valid |= VALID_HACC; hacc = l.getAccuracy(); }
        if (l.hasAltitude()) { valid |= VALID_ALTITUDE; alt = l.getAltitude(); }
        if (l.hasVerticalAccuracy()) { valid |= VALID_VACC; vacc = l.getVerticalAccuracyMeters(); }
        if (l.hasSpeed()) { valid |= VALID_SPEED; speed = l.getSpeed(); }
        if (l.hasSpeedAccuracy()) { valid |= VALID_SPEED_ACC; speedAcc = l.getSpeedAccuracyMetersPerSecond(); }
        if (l.hasBearing()) { valid |= VALID_COURSE; bearing = l.getBearing(); }
        if (l.hasBearingAccuracy()) { valid |= VALID_COURSE_ACC; bearingAcc = l.getBearingAccuracyDegrees(); }
        nativeOnFix(stream, reqPtr, l.getLatitude(), l.getLongitude(), alt, hacc, vacc,
                speed, speedAcc, bearing, bearingAcc, l.getTime(), valid);
    }

    private static String pickProvider(LocationManager lm, double accuracyM) {
        boolean fine = accuracyM <= 100.0;
        if (fine && lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) return LocationManager.GPS_PROVIDER;
        if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) return LocationManager.NETWORK_PROVIDER;
        if (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) return LocationManager.GPS_PROVIDER;
        if (lm.isProviderEnabled(LocationManager.PASSIVE_PROVIDER)) return LocationManager.PASSIVE_PROVIDER;
        return null;
    }

    public static int streamStart(double accuracyM, long minIntervalMs, float minDistanceM) {
        if (!granted()) return RESULT_DENIED;
        LocationManager lm = manager();
        if (lm == null) return RESULT_UNAVAILABLE;
        String provider = pickProvider(lm, accuracyM);
        if (provider == null) return RESULT_UNAVAILABLE;
        streamListener = new LocationListener() {
            @Override public void onLocationChanged(Location l) { deliver(true, 0, l); }
            @Override public void onProviderDisabled(String p) { nativeOnStreamResult(RESULT_UNAVAILABLE); }
            @Override public void onProviderEnabled(String p) { nativeOnStreamResult(RESULT_OK); }
            @Override public void onStatusChanged(String p, int status, Bundle extras) {}
        };
        lm.requestLocationUpdates(provider, minIntervalMs, minDistanceM, streamListener, Looper.getMainLooper());
        return RESULT_OK;
    }

    public static void streamUpdate(double accuracyM, long minIntervalMs, float minDistanceM) {
        streamStop();
        streamStart(accuracyM, minIntervalMs, minDistanceM);
    }

    public static void streamStop() {
        LocationManager lm = manager();
        if (lm != null && streamListener != null) lm.removeUpdates(streamListener);
        streamListener = null;
    }

    public static int requestSingle(final long reqPtr, double accuracyM) {
        if (!granted()) return RESULT_DENIED;
        LocationManager lm = manager();
        if (lm == null) return RESULT_UNAVAILABLE;
        String provider = pickProvider(lm, accuracyM);
        if (provider == null) return RESULT_UNAVAILABLE;
        Context ctx = context();
        if (Build.VERSION.SDK_INT >= 30) {
            CancellationSignal cs = new CancellationSignal();
            singles.put(reqPtr, cs);
            lm.getCurrentLocation(provider, cs, ctx.getMainExecutor(), l -> {
                singles.remove(reqPtr);
                if (l != null) deliver(false, reqPtr, l);
                else nativeOnRequestFailed(reqPtr, RESULT_UNAVAILABLE);
            });
        } else {
            LocationListener once = new LocationListener() {
                @Override public void onLocationChanged(Location l) {
                    singles.remove(reqPtr);
                    deliver(false, reqPtr, l);
                }
                @Override public void onProviderDisabled(String p) {
                    Object o = singles.remove(reqPtr);
                    if (o != null) nativeOnRequestFailed(reqPtr, RESULT_UNAVAILABLE);
                }
                @Override public void onProviderEnabled(String p) {}
                @Override public void onStatusChanged(String p, int status, Bundle extras) {}
            };
            singles.put(reqPtr, once);
            lm.requestSingleUpdate(provider, once, Looper.getMainLooper());
        }
        return RESULT_OK;
    }

    public static void cancelSingle(long reqPtr) {
        Object o = singles.remove(reqPtr);
        if (o instanceof CancellationSignal) ((CancellationSignal) o).cancel();
        else if (o instanceof LocationListener) {
            LocationManager lm = manager();
            if (lm != null) lm.removeUpdates((LocationListener) o);
        }
    }

    public static double[] lastKnown() {
        if (!granted()) return null;
        LocationManager lm = manager();
        if (lm == null) return null;
        Location best = null;
        for (String p : lm.getAllProviders()) {
            Location l = lm.getLastKnownLocation(p);
            if (l != null && (best == null || l.getElapsedRealtimeNanos() > best.getElapsedRealtimeNanos())) best = l;
        }
        if (best == null) return null;
        int valid = VALID_POSITION | VALID_UTC;
        double[] out = new double[12];
        out[0] = best.getLatitude();
        out[1] = best.getLongitude();
        if (best.hasAltitude()) { valid |= VALID_ALTITUDE; out[2] = best.getAltitude(); }
        if (best.hasAccuracy()) { valid |= VALID_HACC; out[3] = best.getAccuracy(); }
        if (best.hasVerticalAccuracy()) { valid |= VALID_VACC; out[4] = best.getVerticalAccuracyMeters(); }
        if (best.hasSpeed()) { valid |= VALID_SPEED; out[5] = best.getSpeed(); }
        if (best.hasSpeedAccuracy()) { valid |= VALID_SPEED_ACC; out[6] = best.getSpeedAccuracyMetersPerSecond(); }
        if (best.hasBearing()) { valid |= VALID_COURSE; out[7] = best.getBearing(); }
        if (best.hasBearingAccuracy()) { valid |= VALID_COURSE_ACC; out[8] = best.getBearingAccuracyDegrees(); }
        out[9] = best.getTime();
        out[10] = ageMillis(best);
        out[11] = valid;
        return out;
    }

    static long ageMillis(Location l) {
        return (android.os.SystemClock.elapsedRealtimeNanos() - l.getElapsedRealtimeNanos()) / 1000000L;
    }

    private static Handler geocoder() {
        if (geocodeHandler == null) {
            HandlerThread t = new HandlerThread("melody-geocoder");
            t.start();
            geocodeHandler = new Handler(t.getLooper());
        }
        return geocodeHandler;
    }

    private static void reportGeocode(long ptr, List<Address> results, int maxResults) {
        if (results == null) {
            nativeOnGeocode(ptr, RESULT_OK, 0, null, null);
            return;
        }
        int count = Math.min(results.size(), maxResults);
        String[] strings = new String[count * 7];
        double[] coords = new double[count * 2];
        for (int i = 0; i < count; i++) {
            Address a = results.get(i);
            strings[i * 7] = a.getFeatureName();
            strings[i * 7 + 1] = a.getThoroughfare();
            strings[i * 7 + 2] = a.getLocality();
            strings[i * 7 + 3] = a.getAdminArea();
            strings[i * 7 + 4] = a.getPostalCode();
            strings[i * 7 + 5] = a.getCountryName();
            strings[i * 7 + 6] = a.getCountryCode();
            coords[i * 2] = a.hasLatitude() ? a.getLatitude() : Double.NaN;
            coords[i * 2 + 1] = a.hasLongitude() ? a.getLongitude() : Double.NaN;
        }
        nativeOnGeocode(ptr, RESULT_OK, count, strings, coords);
    }

    public static void geocodeForward(final long ptr, final String query, final int maxResults) {
        geocoder().post(() -> {
            try {
                List<Address> rs = new Geocoder(context()).getFromLocationName(query, maxResults);
                reportGeocode(ptr, rs, maxResults);
            } catch (IOException e) {
                nativeOnGeocode(ptr, RESULT_UNAVAILABLE, 0, null, null);
            }
        });
    }

    public static void geocodeReverse(final long ptr, final double lat, final double lon, final int maxResults) {
        geocoder().post(() -> {
            try {
                List<Address> rs = new Geocoder(context()).getFromLocation(lat, lon, maxResults);
                reportGeocode(ptr, rs, maxResults);
            } catch (IOException e) {
                nativeOnGeocode(ptr, RESULT_UNAVAILABLE, 0, null, null);
            }
        });
    }
}
