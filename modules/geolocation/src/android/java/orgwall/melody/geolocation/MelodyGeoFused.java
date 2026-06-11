package orgwall.melody.geolocation;

import android.annotation.SuppressLint;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.location.Location;
import android.os.Looper;

import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailabilityLight;
import com.google.android.gms.location.CurrentLocationRequest;
import com.google.android.gms.location.FusedLocationProviderClient;
import com.google.android.gms.location.Geofence;
import com.google.android.gms.location.GeofencingClient;
import com.google.android.gms.location.GeofencingRequest;
import com.google.android.gms.location.LocationAvailability;
import com.google.android.gms.location.LocationCallback;
import com.google.android.gms.location.LocationRequest;
import com.google.android.gms.location.LocationResult;
import com.google.android.gms.location.LocationServices;
import com.google.android.gms.location.Priority;
import com.google.android.gms.tasks.CancellationTokenSource;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

@SuppressLint("MissingPermission")
public final class MelodyGeoFused {
    private static FusedLocationProviderClient client;
    private static GeofencingClient fences;
    private static PendingIntent fencePI;
    private static LocationCallback streamCb;
    private static final Map<Long, CancellationTokenSource> singles = new HashMap<>();

    private MelodyGeoFused() {}

    public static boolean available() {
        try {
            Context ctx = MelodyGeo.context();
            return ctx != null
                    && GoogleApiAvailabilityLight.getInstance().isGooglePlayServicesAvailable(ctx) == ConnectionResult.SUCCESS;
        } catch (Throwable t) {
            return false;
        }
    }

    private static FusedLocationProviderClient client() {
        if (client == null) client = LocationServices.getFusedLocationProviderClient(MelodyGeo.context());
        return client;
    }

    private static GeofencingClient fenceClient() {
        if (fences == null) fences = LocationServices.getGeofencingClient(MelodyGeo.context());
        return fences;
    }

    private static int priority(double accuracyM) {
        if (accuracyM <= 10.0) return Priority.PRIORITY_HIGH_ACCURACY;
        if (accuracyM <= 100.0) return Priority.PRIORITY_BALANCED_POWER_ACCURACY;
        return Priority.PRIORITY_LOW_POWER;
    }

    public static int streamStart(double accuracyM, long minIntervalMs, float minDistanceM) {
        if (!MelodyGeo.granted()) return MelodyGeo.RESULT_DENIED;
        LocationRequest req = new LocationRequest.Builder(priority(accuracyM), Math.max(minIntervalMs, 0))
                .setMinUpdateDistanceMeters(Math.max(minDistanceM, 0f))
                .build();
        streamCb = new LocationCallback() {
            @Override public void onLocationResult(LocationResult r) {
                Location l = r.getLastLocation();
                if (l != null) MelodyGeo.deliver(true, 0, l);
            }
            @Override public void onLocationAvailability(LocationAvailability a) {
                MelodyGeo.nativeOnStreamResult(a.isLocationAvailable() ? MelodyGeo.RESULT_OK : MelodyGeo.RESULT_UNAVAILABLE);
            }
        };
        client().requestLocationUpdates(req, streamCb, Looper.getMainLooper());
        return MelodyGeo.RESULT_OK;
    }

    public static void streamUpdate(double accuracyM, long minIntervalMs, float minDistanceM) {
        streamStop();
        streamStart(accuracyM, minIntervalMs, minDistanceM);
    }

    public static void streamStop() {
        if (streamCb != null) client().removeLocationUpdates(streamCb);
        streamCb = null;
    }

    public static int requestSingle(final long reqPtr, double accuracyM, long maxAgeMs) {
        if (!MelodyGeo.granted()) return MelodyGeo.RESULT_DENIED;
        CurrentLocationRequest creq = new CurrentLocationRequest.Builder()
                .setPriority(priority(accuracyM))
                .setMaximumUpdateAgeMillis(Math.max(maxAgeMs, 0))
                .build();
        CancellationTokenSource cts = new CancellationTokenSource();
        singles.put(reqPtr, cts);
        client().getCurrentLocation(creq, cts.getToken())
                .addOnSuccessListener(l -> {
                    if (singles.remove(reqPtr) == null) return;
                    if (l != null) MelodyGeo.deliver(false, reqPtr, l);
                    else MelodyGeo.nativeOnRequestFailed(reqPtr, MelodyGeo.RESULT_UNAVAILABLE);
                })
                .addOnFailureListener(e -> {
                    if (singles.remove(reqPtr) == null) return;
                    MelodyGeo.nativeOnRequestFailed(reqPtr, MelodyGeo.RESULT_UNAVAILABLE);
                });
        return MelodyGeo.RESULT_OK;
    }

    public static void cancelSingle(long reqPtr) {
        CancellationTokenSource cts = singles.remove(reqPtr);
        if (cts != null) cts.cancel();
    }

    private static PendingIntent fencePendingIntent() {
        if (fencePI == null) {
            Context ctx = MelodyGeo.context();
            Intent i = new Intent(ctx, MelodyGeoFenceReceiver.class);
            fencePI = PendingIntent.getBroadcast(ctx, 0, i,
                    PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        }
        return fencePI;
    }

    public static int regionAdd(final long regionPtr, double lat, double lon, float radiusM,
            boolean notifyEnter, boolean notifyExit) {
        if (!MelodyGeo.granted()) return MelodyGeo.RESULT_DENIED;
        int transitions = (notifyEnter ? Geofence.GEOFENCE_TRANSITION_ENTER : 0)
                | (notifyExit ? Geofence.GEOFENCE_TRANSITION_EXIT : 0);
        Geofence f = new Geofence.Builder()
                .setRequestId(Long.toString(regionPtr))
                .setCircularRegion(lat, lon, radiusM)
                .setExpirationDuration(Geofence.NEVER_EXPIRE)
                .setTransitionTypes(transitions)
                .build();
        GeofencingRequest gr = new GeofencingRequest.Builder().addGeofence(f).setInitialTrigger(0).build();
        fenceClient().addGeofences(gr, fencePendingIntent())
                .addOnFailureListener(e -> MelodyGeo.nativeOnRegionResult(regionPtr, MelodyGeo.RESULT_EXHAUSTED));
        return MelodyGeo.RESULT_OK;
    }

    public static void regionRemove(long regionPtr) {
        fenceClient().removeGeofences(Collections.singletonList(Long.toString(regionPtr)));
    }
}
