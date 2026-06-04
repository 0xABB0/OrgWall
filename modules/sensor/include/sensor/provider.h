#pragma once

#include <sensor/sensor.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64             stable_id;
    str8            name;
    Mel_Sensor_Caps caps;
} Mel_Sensor_Raw;

typedef struct
{
    f32 accel_hz;
    f32 gyro_hz;
} Mel_Sensor_Stream_Config;

typedef struct
{
    void (*on_sample)(void* token, const Mel_Sensor_Reading* reading);
    void (*notify_lost)(void* token);
    void* token;
} Mel_Sensor_Sink;

typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate)(void* user, Mel_Sensor_Raw* out, u32 cap);
    bool (*open)(void* user, u64 stable_id, Mel_Sensor_Descriptor* out);
    void (*close)(void* user, u64 stable_id);

    Mel_Sensor_Status (*query_rates)(void* user, u64 stable_id, Mel_Sensor_Caps* out);

    Mel_Sensor_Status (*start)(void* user, u64 stable_id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink);
    void (*stop)(void* user, u64 stable_id);

    Mel_Sensor_Status (*read)(void* user, u64 stable_id, Mel_Sensor_Reading* out);

    void* (*native)(void* user, u64 stable_id);
} Mel_Sensor_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Sensor_Provider;

Mel_Sensor_Provider mel_sensor_provider_register(const Mel_Sensor_Provider_Desc* desc);
void                mel_sensor_provider_unregister(Mel_Sensor_Provider p);

void mel_sensor__register_host_providers(void);

#ifdef __cplusplus
}
#endif
