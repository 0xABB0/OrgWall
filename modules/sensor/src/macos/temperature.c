#include <sensor.thermal/thermal.h>

#include <IOKit/IOKitLib.h>
#include <pthread.h>
#include <string.h>

#define MEL_SMC_KERNEL_INDEX  2
#define MEL_SMC_READ_BYTES    5
#define MEL_SMC_READ_INDEX    8
#define MEL_SMC_READ_KEYINFO  9

typedef struct { char major, minor, build, reserved[1]; uint16_t release; } Mel_Smc_Vers;
typedef struct { uint16_t version, length; uint32_t cpu_plimit, gpu_plimit, mem_plimit; } Mel_Smc_PLimit;
typedef struct { uint32_t data_size, data_type; char data_attributes; } Mel_Smc_KeyInfo;
typedef char Mel_Smc_Bytes[32];

typedef struct {
    uint32_t       key;
    Mel_Smc_Vers   vers;
    Mel_Smc_PLimit plimit;
    Mel_Smc_KeyInfo key_info;
    char           result, status, data8;
    uint32_t       data32;
    Mel_Smc_Bytes  bytes;
} Mel_Smc_Call;

typedef struct {
    uint32_t      key;
    uint32_t      data_type;
    uint32_t      data_size;
} Mel_Smc_Sensor;

#define MEL_SMC_MAX_SENSORS 96

typedef struct {
    Mel_Smc_Sensor items[MEL_SMC_MAX_SENSORS];
    uint32_t       count;
} Mel_Smc_Group;

static io_connect_t   g_conn;
static bool           g_open;
static Mel_Smc_Group  g_cpu, g_gpu, g_ambient;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static uint32_t mel_smc_fourcc(const char *s) {
    return ((uint32_t)(unsigned char)s[0] << 24) | ((uint32_t)(unsigned char)s[1] << 16)
         | ((uint32_t)(unsigned char)s[2] << 8)  |  (uint32_t)(unsigned char)s[3];
}

static kern_return_t mel_smc_call(Mel_Smc_Call *in, Mel_Smc_Call *out) {
    size_t in_size = sizeof(*in), out_size = sizeof(*out);
    return IOConnectCallStructMethod(g_conn, MEL_SMC_KERNEL_INDEX, in, in_size, out, &out_size);
}

static bool mel_smc_key_info(uint32_t key, uint32_t *type, uint32_t *size) {
    Mel_Smc_Call in = {0}, out = {0};
    in.key = key;
    in.data8 = MEL_SMC_READ_KEYINFO;
    if (mel_smc_call(&in, &out) != kIOReturnSuccess) return false;
    *type = out.key_info.data_type;
    *size = out.key_info.data_size;
    return true;
}

static bool mel_smc_read(const Mel_Smc_Sensor *s, f32 *out_c) {
    Mel_Smc_Call in = {0}, out = {0};
    in.key = s->key;
    in.key_info.data_size = s->data_size;
    in.data8 = MEL_SMC_READ_BYTES;
    if (mel_smc_call(&in, &out) != kIOReturnSuccess) return false;

    if (s->data_type == mel_smc_fourcc("flt ") && s->data_size == 4) {
        f32 v;
        memcpy(&v, out.bytes, 4);
        *out_c = v;
        return true;
    }
    if (s->data_type == mel_smc_fourcc("sp78") && s->data_size >= 2) {
        *out_c = (f32)((int)out.bytes[0] * 256 + (unsigned char)out.bytes[1]) / 256.0f;
        return true;
    }
    return false;
}

static void mel_smc_group_push(Mel_Smc_Group *g, uint32_t key, uint32_t type, uint32_t size) {
    if (g->count < MEL_SMC_MAX_SENSORS)
        g->items[g->count++] = (Mel_Smc_Sensor){ .key = key, .data_type = type, .data_size = size };
}

static void mel_smc_init(void) {
    io_iterator_t it;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleSMC"), &it) != kIOReturnSuccess)
        return;
    io_object_t dev = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!dev) return;
    kern_return_t r = IOServiceOpen(dev, mach_task_self(), 0, &g_conn);
    IOObjectRelease(dev);
    if (r != kIOReturnSuccess) return;
    g_open = true;

    uint32_t count_type, count_size;
    if (!mel_smc_key_info(mel_smc_fourcc("#KEY"), &count_type, &count_size)) return;
    Mel_Smc_Sensor count_key = { .key = mel_smc_fourcc("#KEY"), .data_type = count_type, .data_size = count_size };
    Mel_Smc_Call in = {0}, out = {0};
    in.key = count_key.key;
    in.key_info.data_size = count_key.data_size;
    in.data8 = MEL_SMC_READ_BYTES;
    if (mel_smc_call(&in, &out) != kIOReturnSuccess) return;
    uint32_t total = ((uint32_t)(unsigned char)out.bytes[0] << 24) | ((uint32_t)(unsigned char)out.bytes[1] << 16)
                   | ((uint32_t)(unsigned char)out.bytes[2] << 8)  |  (uint32_t)(unsigned char)out.bytes[3];

    uint32_t flt = mel_smc_fourcc("flt ");
    for (uint32_t i = 0; i < total; i++) {
        Mel_Smc_Call ein = {0}, eout = {0};
        ein.data8 = MEL_SMC_READ_INDEX;
        ein.data32 = i;
        if (mel_smc_call(&ein, &eout) != kIOReturnSuccess) continue;
        uint32_t key = eout.key;
        char c0 = (char)(key >> 24), c1 = (char)(key >> 16);
        if (c0 != 'T') continue;

        uint32_t type, size;
        if (!mel_smc_key_info(key, &type, &size)) continue;
        if (type != flt) continue;

        if (c1 == 'p' || c1 == 'e')      mel_smc_group_push(&g_cpu, key, type, size);
        else if (c1 == 'g')              mel_smc_group_push(&g_gpu, key, type, size);
        else if (c1 == 'A' || c1 == 'a') mel_smc_group_push(&g_ambient, key, type, size);
    }
}

static Mel_Sensor_Temperature mel_smc_group_mean(const Mel_Smc_Group *g) {
    f32 sum = 0.0f;
    uint32_t n = 0;
    for (uint32_t i = 0; i < g->count; i++) {
        f32 c;
        if (!mel_smc_read(&g->items[i], &c)) continue;
        if (c <= 0.0f || c >= 150.0f) continue;
        sum += c;
        n++;
    }
    if (n == 0) return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };
    return (Mel_Sensor_Temperature){ .celsius = sum / (f32)n, .fidelity = MEL_SENSOR_TEMP_MEASURED };
}

Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain) {
    pthread_once(&g_once, mel_smc_init);
    if (!g_open) return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };

    switch (domain) {
        case MEL_SENSOR_TEMP_DOMAIN_CPU:
        case MEL_SENSOR_TEMP_DOMAIN_PRIMARY: return mel_smc_group_mean(&g_cpu);
        case MEL_SENSOR_TEMP_DOMAIN_GPU:     return mel_smc_group_mean(&g_gpu);
        case MEL_SENSOR_TEMP_DOMAIN_AMBIENT: {
            Mel_Sensor_Temperature t = mel_smc_group_mean(&g_ambient);
            if (t.fidelity == MEL_SENSOR_TEMP_MEASURED) t.fidelity = MEL_SENSOR_TEMP_DERIVED;
            return t;
        }
    }
    return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };
}
