#include <sensor/provider.h>
#include <log/log.h>

#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

#define MEL_SENSOR_IOS_STABLE_ID 0x696F73696D750000ULL
#define MEL_SENSOR_G             9.80665

static CMMotionManager* g_motion;
static Mel_Sensor_Sink  g_sink;
static bool             g_streaming;

static CMMotionManager* motion(void)
{
    if (!g_motion)
        g_motion = [[CMMotionManager alloc] init];
    return g_motion;
}

static u32 ios_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0)
        return 0;
    CMMotionManager* m = motion();
    bool             accel = m.accelerometerAvailable;
    bool             gyro = m.gyroAvailable;
    if (!accel && !gyro)
        return 0;
    out[0] = (Mel_Sensor_Raw){
        .stable_id = MEL_SENSOR_IOS_STABLE_ID,
        .name = S8("Core Motion IMU"),
        .caps = {
            .has_accel = accel,
            .has_gyro = gyro,
            .accel_min_hz = 1.0f,
            .accel_max_hz = 100.0f,
            .gyro_min_hz = 1.0f,
            .gyro_max_hz = 100.0f,
            .accel_range_mps2 = (f32)(8.0 * MEL_SENSOR_G),
            .gyro_range_radps = 35.0f,
            .requires_permission = false,
            .hardware_timestamps = true,
            .side = MEL_SENSOR_SIDE_UNSPECIFIED,
        },
    };
    return 1;
}

static Mel_Sensor_Status ios_query_rates(void* user, u64 stable_id, Mel_Sensor_Caps* out)
{
    (void)user;
    (void)stable_id;
    CMMotionManager* m = motion();
    out->has_accel = m.accelerometerAvailable;
    out->has_gyro = m.gyroAvailable;
    out->accel_min_hz = 1.0f;
    out->accel_max_hz = 100.0f;
    out->gyro_min_hz = 1.0f;
    out->gyro_max_hz = 100.0f;
    return MEL_SENSOR_OK;
}

static void emit_combined(void)
{
    CMMotionManager* m = g_motion;
    if (!m || !g_sink.on_sample)
        return;
    CMAccelerometerData* a = m.accelerometerData;
    CMGyroData*          gy = m.gyroData;
    Mel_Sensor_Reading   r = { 0 };
    if (a)
    {
        r.timestamp_s = a.timestamp;
        r.accel_mps2[0] = (f32)(a.acceleration.x * MEL_SENSOR_G);
        r.accel_mps2[1] = (f32)(a.acceleration.y * MEL_SENSOR_G);
        r.accel_mps2[2] = (f32)(a.acceleration.z * MEL_SENSOR_G);
        r.valid_mask |= MEL_SENSOR_VALID_ACCEL;
    }
    if (gy)
    {
        if (gy.timestamp > r.timestamp_s)
            r.timestamp_s = gy.timestamp;
        r.gyro_radps[0] = (f32)gy.rotationRate.x;
        r.gyro_radps[1] = (f32)gy.rotationRate.y;
        r.gyro_radps[2] = (f32)gy.rotationRate.z;
        r.valid_mask |= MEL_SENSOR_VALID_GYRO;
    }
    if (r.valid_mask)
        g_sink.on_sample(g_sink.token, &r);
}

static Mel_Sensor_Status ios_start(void* user, u64 stable_id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink)
{
    (void)user;
    (void)stable_id;
    CMMotionManager* m = motion();
    if (!m.accelerometerAvailable && !m.gyroAvailable)
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE;
    g_sink = sink;
    g_streaming = true;

    f32 accel_hz = cfg->accel_hz > 0.0f ? cfg->accel_hz : 60.0f;
    f32 gyro_hz = cfg->gyro_hz > 0.0f ? cfg->gyro_hz : 60.0f;

    NSOperationQueue* q = [NSOperationQueue mainQueue];
    if (m.accelerometerAvailable)
    {
        m.accelerometerUpdateInterval = 1.0 / (double)accel_hz;
        [m startAccelerometerUpdatesToQueue:q
                                withHandler:^(CMAccelerometerData* data, NSError* err) {
                                    (void)data;
                                    if (err)
                                    {
                                        if (g_sink.notify_lost)
                                            g_sink.notify_lost(g_sink.token);
                                        return;
                                    }
                                    emit_combined();
                                }];
    }
    if (m.gyroAvailable)
    {
        m.gyroUpdateInterval = 1.0 / (double)gyro_hz;
        [m startGyroUpdatesToQueue:q
                       withHandler:^(CMGyroData* data, NSError* err) {
                           (void)data;
                           if (err)
                           {
                               if (g_sink.notify_lost)
                                   g_sink.notify_lost(g_sink.token);
                               return;
                           }
                           emit_combined();
                       }];
    }
    return MEL_SENSOR_OK;
}

static void ios_stop(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    if (!g_motion)
        return;
    [g_motion stopAccelerometerUpdates];
    [g_motion stopGyroUpdates];
    g_streaming = false;
}

static Mel_Sensor_Status ios_read(void* user, u64 stable_id, Mel_Sensor_Reading* out)
{
    (void)user;
    (void)stable_id;
    CMMotionManager* m = g_motion;
    if (!m || !g_streaming)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NOT_STREAMING;
    CMAccelerometerData* a = m.accelerometerData;
    CMGyroData*          gy = m.gyroData;
    if (!a && !gy)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NO_DATA;
    *out = (Mel_Sensor_Reading){ 0 };
    if (a)
    {
        out->timestamp_s = a.timestamp;
        out->accel_mps2[0] = (f32)(a.acceleration.x * MEL_SENSOR_G);
        out->accel_mps2[1] = (f32)(a.acceleration.y * MEL_SENSOR_G);
        out->accel_mps2[2] = (f32)(a.acceleration.z * MEL_SENSOR_G);
        out->valid_mask |= MEL_SENSOR_VALID_ACCEL;
    }
    if (gy)
    {
        if (gy.timestamp > out->timestamp_s)
            out->timestamp_s = gy.timestamp;
        out->gyro_radps[0] = (f32)gy.rotationRate.x;
        out->gyro_radps[1] = (f32)gy.rotationRate.y;
        out->gyro_radps[2] = (f32)gy.rotationRate.z;
        out->valid_mask |= MEL_SENSOR_VALID_GYRO;
    }
    return MEL_SENSOR_OK;
}

static void* ios_native(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    return (__bridge void*)motion();
}

void mel_sensor__register_host_providers(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "ios-coremotion",
        .enumerate = ios_enumerate,
        .query_rates = ios_query_rates,
        .start = ios_start,
        .stop = ios_stop,
        .read = ios_read,
        .native = ios_native,
    };
    mel_sensor_provider_register(&desc);
}
