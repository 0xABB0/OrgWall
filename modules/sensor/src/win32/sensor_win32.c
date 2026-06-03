#include <core/platform.h>

#if !MEL_PLATFORM_WINDOWS
#error "win32-only translation unit"
#endif

#include <sensor/provider.h>
#include <log/log.h>

#if __has_include(<sensorsapi.h>) && __has_include(<sensors.h>)
#define MEL_SENSOR_WIN32_COM 1
#else
#define MEL_SENSOR_WIN32_COM 0
#endif

#if MEL_SENSOR_WIN32_COM

#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <sensorsapi.h>
#include <sensors.h>
#include <propvarutil.h>

#define MEL_SENSOR_WIN32_STABLE_ID 0x77696E696D750000ULL
#define MEL_SENSOR_G               9.80665
#define MEL_SENSOR_DEG2RAD         0.017453292519943295

static struct
{
    bool            resolved;
    bool            failed;
    ISensorManager* mgr;
    ISensor*        accel;
    ISensor*        gyro;
    bool            streaming;
    Mel_Sensor_Sink sink;
} g;

static bool com_resolve(void)
{
    if (g.resolved)
        return !g.failed;
    g.resolved = true;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool    owned = SUCCEEDED(hr);
    (void)owned;

    hr = CoCreateInstance(&CLSID_SensorManager, NULL, CLSCTX_INPROC_SERVER, &IID_ISensorManager, (void**)&g.mgr);
    if (FAILED(hr) || !g.mgr)
    {
        mel_log_warn("sensor", "win32: ISensorManager unavailable (hr=0x%08lx)", (unsigned long)hr);
        g.failed = true;
        return false;
    }

    ISensorCollection* col = NULL;
    if (SUCCEEDED(ISensorManager_GetSensorsByType(g.mgr, &SENSOR_TYPE_ACCELEROMETER_3D, &col)) && col)
    {
        ISensorCollection_GetAt(col, 0, &g.accel);
        ISensorCollection_Release(col);
    }
    col = NULL;
    if (SUCCEEDED(ISensorManager_GetSensorsByType(g.mgr, &SENSOR_TYPE_GYROMETER_3D, &col)) && col)
    {
        ISensorCollection_GetAt(col, 0, &g.gyro);
        ISensorCollection_Release(col);
    }
    return true;
}

static f32 read_axis(ISensorDataReport* rep, REFPROPERTYKEY key)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    f32 out = 0.0f;
    if (SUCCEEDED(ISensorDataReport_GetSensorValue(rep, key, &v)) && v.vt == VT_R8)
        out = (f32)v.dblVal;
    PropVariantClear(&v);
    return out;
}

static u32 win32_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0 || !com_resolve())
        return 0;
    if (!g.accel && !g.gyro)
        return 0;
    out[0] = (Mel_Sensor_Raw){
        .stable_id = MEL_SENSOR_WIN32_STABLE_ID,
        .name = S8("Windows Sensor API"),
        .caps = {
            .has_accel = g.accel != NULL,
            .has_gyro = g.gyro != NULL,
            .accel_min_hz = 1.0f,
            .accel_max_hz = 100.0f,
            .gyro_min_hz = 1.0f,
            .gyro_max_hz = 100.0f,
            .requires_permission = true,
            .hardware_timestamps = true,
            .side = MEL_SENSOR_SIDE_UNSPECIFIED,
        },
    };
    return 1;
}

static Mel_Sensor_Status win32_read(void* user, u64 stable_id, Mel_Sensor_Reading* out)
{
    (void)user;
    (void)stable_id;
    if (!com_resolve())
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE;
    *out = (Mel_Sensor_Reading){ 0 };
    out->timestamp_s = (f64)GetTickCount64() * 1e-3;

    if (g.accel)
    {
        ISensorDataReport* rep = NULL;
        if (SUCCEEDED(ISensor_GetData(g.accel, &rep)) && rep)
        {
            out->accel_mps2[0] = (f32)(read_axis(rep, &SENSOR_DATA_TYPE_ACCELERATION_X_G) * MEL_SENSOR_G);
            out->accel_mps2[1] = (f32)(read_axis(rep, &SENSOR_DATA_TYPE_ACCELERATION_Y_G) * MEL_SENSOR_G);
            out->accel_mps2[2] = (f32)(read_axis(rep, &SENSOR_DATA_TYPE_ACCELERATION_Z_G) * MEL_SENSOR_G);
            out->valid_mask |= MEL_SENSOR_VALID_ACCEL;
            ISensorDataReport_Release(rep);
        }
    }
    if (g.gyro)
    {
        ISensorDataReport* rep = NULL;
        if (SUCCEEDED(ISensor_GetData(g.gyro, &rep)) && rep)
        {
            out->gyro_radps[0] = (f32)(read_axis(rep, &SENSOR_DATA_TYPE_ANGULAR_VELOCITY_X_DEGREES_PER_SECOND) * MEL_SENSOR_DEG2RAD);
            out->gyro_radps[1] = (f32)(read_axis(rep, &SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Y_DEGREES_PER_SECOND) * MEL_SENSOR_DEG2RAD);
            out->gyro_radps[2] = (f32)(read_axis(rep, &SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Z_DEGREES_PER_SECOND) * MEL_SENSOR_DEG2RAD);
            out->valid_mask |= MEL_SENSOR_VALID_GYRO;
            ISensorDataReport_Release(rep);
        }
    }
    if (!out->valid_mask)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NO_DATA;
    return MEL_SENSOR_OK;
}

static Mel_Sensor_Status win32_start(void* user, u64 stable_id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink)
{
    (void)user;
    (void)stable_id;
    (void)cfg;
    if (!com_resolve())
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE;
    g.sink = sink;
    g.streaming = true;
    return MEL_SENSOR_OK;
}

static void win32_stop(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    g.streaming = false;
}

void mel_sensor__register_host_providers(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "win32-sensorapi",
        .enumerate = win32_enumerate,
        .start = win32_start,
        .stop = win32_stop,
        .read = win32_read,
    };
    mel_sensor_provider_register(&desc);
}

#else

static u32 win32_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    (void)out;
    (void)cap;
    return 0;
}

void mel_sensor__register_host_providers(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "win32-none",
        .enumerate = win32_enumerate,
    };
    mel_sensor_provider_register(&desc);
    mel_log_warn("sensor", "win32: <sensorsapi.h> absent in this toolchain; Sensor API backend disabled, host is honest-absent");
}

#endif
