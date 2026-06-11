#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "wasm/emscripten-only translation unit"
#endif

#include <sensor/provider.h>
#include <log/log.h>

#include <emscripten.h>

#define MEL_SENSOR_WASM_STABLE_ID 0x7761736D696D7500ULL

static Mel_Sensor_Sink    g_sink;
static Mel_Sensor_Reading g_latest;
static bool               g_have_latest;
static bool               g_streaming;

EM_JS(int, mel_sensor_wasm_query, (), {
    var hasGS = (typeof Accelerometer !== 'undefined') || (typeof Gyroscope !== 'undefined');
    var hasDM = (typeof DeviceMotionEvent !== 'undefined');
    if (hasGS)
        return 3;
    if (hasDM)
        return 1;
    return 0;
});

EM_JS(void, mel_sensor_wasm_start, (float accelHz, float gyroHz), {
    Module._melSensorAccel = null;
    Module._melSensorGyro = null;
    Module._melSensorDM = null;

    var deliver = function(ts, ax, ay, az, av, gx, gy, gz, gv)
    {
        var ptr = Module._mel_sensor_wasm_alloc_reading();
        Module.HEAPF64[(ptr) >> 3] = ts;
        Module.HEAPF32[(ptr + 8) >> 2] = ax;
        Module.HEAPF32[(ptr + 12) >> 2] = ay;
        Module.HEAPF32[(ptr + 16) >> 2] = az;
        Module.HEAPF32[(ptr + 20) >> 2] = gx;
        Module.HEAPF32[(ptr + 24) >> 2] = gy;
        Module.HEAPF32[(ptr + 28) >> 2] = gz;
        var mask = (av ? 1 : 0) | (gv ? 2 : 0);
        Module._mel_sensor_wasm_commit_reading(mask);
    };
    Module._melSensorDeliver = deliver;

    if (typeof Accelerometer !== 'undefined')
    {
        try
        {
            var a = new Accelerometer({ frequency : accelHz > 0 ? accelHz : 60 });
            a.addEventListener('reading', function() { deliver(performance.now() * 1e-3, a.x, a.y, a.z, true, 0, 0, 0, false); });
            a.start();
            Module._melSensorAccel = a;
        }
        catch(e) {}
    }
    if (typeof Gyroscope !== 'undefined')
    {
        try
        {
            var gptr = new Gyroscope({ frequency : gyroHz > 0 ? gyroHz : 60 });
            gptr.addEventListener('reading', function() { deliver(performance.now() * 1e-3, 0, 0, 0, false, gptr.x, gptr.y, gptr.z, true); });
            gptr.start();
            Module._melSensorGyro = gptr;
        }
        catch(e) {}
    }
    if (!Module._melSensorAccel && !Module._melSensorGyro && typeof DeviceMotionEvent !== 'undefined')
    {
        var d2r = Math.PI / 180.0;
        var handler = function(ev)
        {
            var ag = ev.accelerationIncludingGravity || { x : 0, y : 0, z : 0 };
            var rr = ev.rotationRate || { alpha : 0, beta : 0, gamma : 0 };
            deliver(performance.now() * 1e-3, ag.x || 0, ag.y || 0, ag.z || 0, true, (rr.beta || 0) * d2r, (rr.gamma || 0) * d2r, (rr.alpha || 0) * d2r, true);
        };
        window.addEventListener('devicemotion', handler);
        Module._melSensorDM = handler;
    }
});

EM_JS(void, mel_sensor_wasm_stop, (), {
    if (Module._melSensorAccel)
    {
        try { Module._melSensorAccel.stop(); }
        catch(e) {}
        Module._melSensorAccel = null;
    }
    if (Module._melSensorGyro)
    {
        try { Module._melSensorGyro.stop(); }
        catch(e) {}
        Module._melSensorGyro = null;
    }
    if (Module._melSensorDM)
    {
        window.removeEventListener('devicemotion', Module._melSensorDM);
        Module._melSensorDM = null;
    }
});

static Mel_Sensor_Reading g_pending;

EMSCRIPTEN_KEEPALIVE void* mel_sensor_wasm_alloc_reading(void)
{
    g_pending = (Mel_Sensor_Reading){ 0 };
    return &g_pending;
}

EMSCRIPTEN_KEEPALIVE void mel_sensor_wasm_commit_reading(int mask)
{
    g_pending.valid_mask = (u32)mask;
    g_latest = g_pending;
    g_have_latest = true;
    if (g_sink.on_sample)
        g_sink.on_sample(g_sink.token, &g_latest);
}

static u32 wasm_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0)
        return 0;
    int avail = mel_sensor_wasm_query();
    if (avail == 0)
        return 0;
    out[0] = (Mel_Sensor_Raw){
        .stable_id = MEL_SENSOR_WASM_STABLE_ID,
        .name = S8("Web Sensor API"),
        .caps = {
            .has_accel = true,
            .has_gyro = (avail & 2) != 0,
            .accel_min_hz = 1.0f,
            .accel_max_hz = 60.0f,
            .gyro_min_hz = 1.0f,
            .gyro_max_hz = 60.0f,
            .requires_permission = true,
            .hardware_timestamps = false,
            .side = MEL_SENSOR_SIDE_UNSPECIFIED,
        },
    };
    return 1;
}

static Mel_Sensor_Status wasm_start(void* user, u64 stable_id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink)
{
    (void)user;
    (void)stable_id;
    if (mel_sensor_wasm_query() == 0)
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE;
    g_sink = sink;
    g_streaming = true;
    mel_sensor_wasm_start(cfg->accel_hz, cfg->gyro_hz);
    return MEL_SENSOR_OK | MEL_SENSOR_WARN_PERMISSION_NEEDED | MEL_SENSOR_WARNED;
}

static void wasm_stop(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    mel_sensor_wasm_stop();
    g_streaming = false;
}

static Mel_Sensor_Status wasm_read(void* user, u64 stable_id, Mel_Sensor_Reading* out)
{
    (void)user;
    (void)stable_id;
    if (!g_streaming)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NOT_STREAMING;
    if (!g_have_latest)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NO_DATA;
    *out = g_latest;
    return MEL_SENSOR_OK;
}

void mel_sensor__register_host_providers(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "wasm-websensor",
        .enumerate = wasm_enumerate,
        .start = wasm_start,
        .stop = wasm_stop,
        .read = wasm_read,
    };
    mel_sensor_provider_register(&desc);
}
