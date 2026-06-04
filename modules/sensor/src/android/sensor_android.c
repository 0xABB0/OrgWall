#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <jni.h>
#include <string.h>

#include <platform/android/jni.h>

#include <sensor/provider.h>
#include <log/log.h>

#define MEL_SENSOR_ANDROID_STABLE_ID 0x616E64696D750000ULL

static struct
{
    bool      resolved;
    bool      failed;
    jclass    cls;
    jmethodID m_query;
    jmethodID m_start;
    jmethodID m_stop;
    jmethodID m_has_accel;
    jmethodID m_has_gyro;
    jmethodID m_accel_max_hz;
    jmethodID m_gyro_max_hz;
} g;

static Mel_Sensor_Sink g_sink;

static bool resolve(JNIEnv* env)
{
    if (g.resolved)
        return !g.failed;
    g.resolved = true;

    jclass local = (*env)->FindClass(env, "orgwall/melody/sensor/MelodySensor");
    if (!local)
    {
        (*env)->ExceptionClear(env);
        g.failed = true;
        return false;
    }
    g.cls = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);

    g.m_query = (*env)->GetStaticMethodID(env, g.cls, "query", "()Z");
    g.m_start = (*env)->GetStaticMethodID(env, g.cls, "start", "(FF)Z");
    g.m_stop = (*env)->GetStaticMethodID(env, g.cls, "stop", "()V");
    g.m_has_accel = (*env)->GetStaticMethodID(env, g.cls, "hasAccel", "()Z");
    g.m_has_gyro = (*env)->GetStaticMethodID(env, g.cls, "hasGyro", "()Z");
    g.m_accel_max_hz = (*env)->GetStaticMethodID(env, g.cls, "accelMaxHz", "()F");
    g.m_gyro_max_hz = (*env)->GetStaticMethodID(env, g.cls, "gyroMaxHz", "()F");

    if (!g.m_query || !g.m_start || !g.m_stop || !g.m_has_accel || !g.m_has_gyro || !g.m_accel_max_hz || !g.m_gyro_max_hz)
    {
        (*env)->ExceptionClear(env);
        g.failed = true;
        return false;
    }
    return true;
}

JNIEXPORT void JNICALL Java_orgwall_melody_sensor_MelodySensor_nativeSample(JNIEnv* env, jclass cls, jdouble ts, jfloat ax, jfloat ay, jfloat az, jboolean accel_valid, jfloat gx, jfloat gy, jfloat gz, jboolean gyro_valid)
{
    (void)env;
    (void)cls;
    if (!g_sink.on_sample)
        return;
    Mel_Sensor_Reading r = { 0 };
    r.timestamp_s = (f64)ts;
    if (accel_valid)
    {
        r.accel_mps2[0] = (f32)ax;
        r.accel_mps2[1] = (f32)ay;
        r.accel_mps2[2] = (f32)az;
        r.valid_mask |= MEL_SENSOR_VALID_ACCEL;
    }
    if (gyro_valid)
    {
        r.gyro_radps[0] = (f32)gx;
        r.gyro_radps[1] = (f32)gy;
        r.gyro_radps[2] = (f32)gz;
        r.valid_mask |= MEL_SENSOR_VALID_GYRO;
    }
    if (r.valid_mask)
        g_sink.on_sample(g_sink.token, &r);
}

static u32 android_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0)
        return 0;
    JNIEnv* env = mel_platform_android_env();
    if (!env || !resolve(env))
        return 0;
    if (!(*env)->CallStaticBooleanMethod(env, g.cls, g.m_query))
        return 0;

    bool accel = (*env)->CallStaticBooleanMethod(env, g.cls, g.m_has_accel);
    bool gyro = (*env)->CallStaticBooleanMethod(env, g.cls, g.m_has_gyro);
    if (!accel && !gyro)
        return 0;
    f32 amax = (*env)->CallStaticFloatMethod(env, g.cls, g.m_accel_max_hz);
    f32 gmax = (*env)->CallStaticFloatMethod(env, g.cls, g.m_gyro_max_hz);

    out[0] = (Mel_Sensor_Raw){
        .stable_id = MEL_SENSOR_ANDROID_STABLE_ID,
        .name = S8("Android SensorManager"),
        .caps = {
            .has_accel = accel,
            .has_gyro = gyro,
            .accel_min_hz = 1.0f,
            .accel_max_hz = amax > 0.0f ? amax : 100.0f,
            .gyro_min_hz = 1.0f,
            .gyro_max_hz = gmax > 0.0f ? gmax : 100.0f,
            .requires_permission = false,
            .hardware_timestamps = true,
            .side = MEL_SENSOR_SIDE_UNSPECIFIED,
        },
    };
    return 1;
}

static Mel_Sensor_Status android_start(void* user, u64 stable_id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink)
{
    (void)user;
    (void)stable_id;
    JNIEnv* env = mel_platform_android_env();
    if (!env || !resolve(env))
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE;
    g_sink = sink;
    jboolean ok = (*env)->CallStaticBooleanMethod(env, g.cls, g.m_start, (jfloat)cfg->accel_hz, (jfloat)cfg->gyro_hz);
    return ok ? MEL_SENSOR_OK : (MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE);
}

static void android_stop(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    JNIEnv* env = mel_platform_android_env();
    if (!env || !resolve(env))
        return;
    (*env)->CallStaticVoidMethod(env, g.cls, g.m_stop);
}

void mel_sensor__register_host_providers(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "android-sensormanager",
        .enumerate = android_enumerate,
        .start = android_start,
        .stop = android_stop,
    };
    mel_sensor_provider_register(&desc);
}
