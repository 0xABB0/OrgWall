package orgwall.melody.sensor;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;

import java.lang.reflect.Method;

public final class MelodySensor {
    private static Context context;
    private static SensorManager manager;
    private static Sensor accel;
    private static Sensor gyro;
    private static boolean inited;

    private static volatile boolean haveAccel;
    private static volatile float ax, ay, az;
    private static volatile boolean haveGyro;
    private static volatile float gx, gy, gz;
    private static volatile double ts;

    private MelodySensor() {}

    private static final SensorEventListener listener = new SensorEventListener() {
        @Override public void onSensorChanged(SensorEvent e) {
            ts = e.timestamp * 1e-9;
            if (e.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
                ax = e.values[0]; ay = e.values[1]; az = e.values[2];
                haveAccel = true;
            } else if (e.sensor.getType() == Sensor.TYPE_GYROSCOPE) {
                gx = e.values[0]; gy = e.values[1]; gz = e.values[2];
                haveGyro = true;
            }
            nativeSample(ts, ax, ay, az, haveAccel, gx, gy, gz, haveGyro);
        }
        @Override public void onAccuracyChanged(Sensor s, int a) {}
    };

    public static synchronized boolean query() {
        if (inited) return manager != null;
        inited = true;
        context = resolveAppContext();
        if (context == null) return false;
        manager = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
        if (manager == null) return false;
        accel = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        gyro = manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        return true;
    }

    public static boolean hasAccel() { return accel != null; }
    public static boolean hasGyro() { return gyro != null; }

    public static float accelMaxHz() { return maxHz(accel); }
    public static float gyroMaxHz() { return maxHz(gyro); }

    private static float maxHz(Sensor s) {
        if (s == null) return 0f;
        int minDelayUs = s.getMinDelay();
        if (minDelayUs <= 0) return 0f;
        return 1_000_000f / (float) minDelayUs;
    }

    public static synchronized boolean start(float accelHz, float gyroHz) {
        if (!query()) return false;
        boolean any = false;
        if (accel != null) {
            manager.registerListener(listener, accel, periodUs(accelHz));
            any = true;
        }
        if (gyro != null) {
            manager.registerListener(listener, gyro, periodUs(gyroHz));
            any = true;
        }
        return any;
    }

    public static synchronized void stop() {
        if (manager != null)
            manager.unregisterListener(listener);
        haveAccel = false;
        haveGyro = false;
    }

    private static int periodUs(float hz) {
        if (hz <= 0f) return SensorManager.SENSOR_DELAY_GAME;
        return Math.max(1, Math.round(1_000_000f / hz));
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

    private static native void nativeSample(double ts, float ax, float ay, float az, boolean accelValid, float gx, float gy, float gz, boolean gyroValid);
}
