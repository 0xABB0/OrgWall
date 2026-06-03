#include <core/platform.h>

#if !MEL_PLATFORM_LINUX
#error "linux-only translation unit"
#endif

#include <sensor/provider.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <log/log.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MEL_SENSOR_IIO_ROOT "/sys/bus/iio/devices"

typedef struct
{
    bool open;
    char path[256];
    bool has_accel, has_gyro;
    f32  accel_scale[3];
    f32  gyro_scale[3];
    bool streaming;
} Linux_Sensor;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Linux_Sensor) devices;
} Linux_Backend;

static Linux_Backend g_lx;

static f64 mono_now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1e-9;
}

static bool read_f32_file(const char* dir, const char* leaf, f32* out)
{
    char path[320];
    snprintf(path, sizeof path, "%s/%s", dir, leaf);
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    double v = 0.0;
    int    ok = fscanf(f, "%lf", &v);
    fclose(f);
    if (ok != 1)
        return false;
    *out = (f32)v;
    return true;
}

static bool channel_present(const char* dir, const char* leaf)
{
    char path[320];
    snprintf(path, sizeof path, "%s/%s", dir, leaf);
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static void probe_device(const char* dir)
{
    bool accel = channel_present(dir, "in_accel_x_raw");
    bool gyro = channel_present(dir, "in_anglvel_x_raw");
    if (!accel && !gyro)
        return;

    Linux_Sensor s = { .open = true, .has_accel = accel, .has_gyro = gyro };
    strncpy(s.path, dir, sizeof s.path - 1);

    f32 ascale = 1.0f, gscale = 1.0f;
    read_f32_file(dir, "in_accel_scale", &ascale);
    read_f32_file(dir, "in_anglvel_scale", &gscale);
    for (int i = 0; i < 3; i++)
    {
        s.accel_scale[i] = ascale;
        s.gyro_scale[i] = gscale;
    }
    mel_array_push(&g_lx.devices, s);
}

static u64 stable_id_for(usize index, const char* dir)
{
    u64 h = 1469598103934665603ULL;
    for (const char* p = dir; *p; p++)
    {
        h ^= (u8)*p;
        h *= 1099511628211ULL;
    }
    return h ^ ((u64)index << 56);
}

static Linux_Sensor* device_by_id(u64 id)
{
    for (usize i = 0; i < g_lx.devices.count; i++)
        if (g_lx.devices.items[i].open && stable_id_for(i, g_lx.devices.items[i].path) == id)
            return &g_lx.devices.items[i];
    return NULL;
}

static u32 linux_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    for (usize i = 0; i < g_lx.devices.count; i++)
        g_lx.devices.items[i].open = false;
    g_lx.devices.count = 0;

    DIR* d = opendir(MEL_SENSOR_IIO_ROOT);
    if (!d)
        return 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (strncmp(e->d_name, "iio:device", 10) != 0)
            continue;
        char dir[256];
        snprintf(dir, sizeof dir, "%s/%s", MEL_SENSOR_IIO_ROOT, e->d_name);
        probe_device(dir);
    }
    closedir(d);

    u32 n = 0;
    for (usize i = 0; i < g_lx.devices.count && n < cap; i++)
    {
        Linux_Sensor* s = &g_lx.devices.items[i];
        out[n++] = (Mel_Sensor_Raw){
            .stable_id = stable_id_for(i, s->path),
            .name = S8("iio-imu"),
            .caps = {
                .has_accel = s->has_accel,
                .has_gyro = s->has_gyro,
                .accel_min_hz = 1.0f,
                .accel_max_hz = 200.0f,
                .gyro_min_hz = 1.0f,
                .gyro_max_hz = 200.0f,
                .requires_permission = false,
                .hardware_timestamps = false,
                .side = MEL_SENSOR_SIDE_UNSPECIFIED,
            },
        };
    }
    return n;
}

static Mel_Sensor_Status linux_start(void* user, u64 stable_id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink)
{
    (void)user;
    (void)cfg;
    (void)sink;
    Linux_Sensor* s = device_by_id(stable_id);
    if (!s)
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_DEVICE_LOST;
    s->streaming = true;
    return MEL_SENSOR_OK;
}

static void linux_stop(void* user, u64 stable_id)
{
    (void)user;
    Linux_Sensor* s = device_by_id(stable_id);
    if (s)
        s->streaming = false;
}

static Mel_Sensor_Status linux_read(void* user, u64 stable_id, Mel_Sensor_Reading* out)
{
    (void)user;
    Linux_Sensor* s = device_by_id(stable_id);
    if (!s)
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_DEVICE_LOST;
    *out = (Mel_Sensor_Reading){ 0 };
    out->timestamp_s = mono_now_s();

    if (s->has_accel)
    {
        f32  x = 0, y = 0, z = 0;
        bool ok = read_f32_file(s->path, "in_accel_x_raw", &x) && read_f32_file(s->path, "in_accel_y_raw", &y) && read_f32_file(s->path, "in_accel_z_raw", &z);
        if (ok)
        {
            out->accel_mps2[0] = x * s->accel_scale[0];
            out->accel_mps2[1] = y * s->accel_scale[1];
            out->accel_mps2[2] = z * s->accel_scale[2];
            out->valid_mask |= MEL_SENSOR_VALID_ACCEL;
        }
    }
    if (s->has_gyro)
    {
        f32  x = 0, y = 0, z = 0;
        bool ok = read_f32_file(s->path, "in_anglvel_x_raw", &x) && read_f32_file(s->path, "in_anglvel_y_raw", &y) && read_f32_file(s->path, "in_anglvel_z_raw", &z);
        if (ok)
        {
            out->gyro_radps[0] = x * s->gyro_scale[0];
            out->gyro_radps[1] = y * s->gyro_scale[1];
            out->gyro_radps[2] = z * s->gyro_scale[2];
            out->valid_mask |= MEL_SENSOR_VALID_GYRO;
        }
    }
    if (!out->valid_mask)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NO_DATA;
    return MEL_SENSOR_OK;
}

void mel_sensor__register_host_providers(void)
{
    g_lx.alloc = mel_alloc_heap();
    mel_array_init(&g_lx.devices, g_lx.alloc);
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "linux-iio",
        .enumerate = linux_enumerate,
        .start = linux_start,
        .stop = linux_stop,
        .read = linux_read,
    };
    mel_sensor_provider_register(&desc);
}
