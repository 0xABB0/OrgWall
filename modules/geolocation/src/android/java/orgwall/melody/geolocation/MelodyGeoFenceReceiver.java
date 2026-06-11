package orgwall.melody.geolocation;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import com.google.android.gms.location.Geofence;
import com.google.android.gms.location.GeofencingEvent;

import java.util.List;

public final class MelodyGeoFenceReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        GeofencingEvent ev = GeofencingEvent.fromIntent(intent);
        if (ev == null || ev.hasError()) return;
        int transition = ev.getGeofenceTransition();
        if (transition != Geofence.GEOFENCE_TRANSITION_ENTER && transition != Geofence.GEOFENCE_TRANSITION_EXIT) return;
        List<Geofence> triggered = ev.getTriggeringGeofences();
        if (triggered == null) return;
        boolean entered = transition == Geofence.GEOFENCE_TRANSITION_ENTER;
        for (Geofence f : triggered) {
            try {
                MelodyGeo.nativeOnRegion(Long.parseLong(f.getRequestId()), entered);
            } catch (NumberFormatException ignored) {
            }
        }
    }
}
