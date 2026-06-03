#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_Sensor_Status;

#define MEL_SENSOR_SEVERITY_MASK          0x3u
#define MEL_SENSOR_OK                     0u
#define MEL_SENSOR_WARNED                 1u
#define MEL_SENSOR_ERROR                  2u

#define MEL_SENSOR_WARN_RATE_CLAMPED      (1u << 2)
#define MEL_SENSOR_WARN_RATE_QUANTIZED    (1u << 3)
#define MEL_SENSOR_WARN_AXIS_SYNTHESIZED  (1u << 4)
#define MEL_SENSOR_WARN_TIMESTAMP_SYNTHED (1u << 5)
#define MEL_SENSOR_WARN_PERMISSION_NEEDED (1u << 6)

#define MEL_SENSOR_RESULT_DEVICE_LOST     (1u << 8)
#define MEL_SENSOR_RESULT_UNAVAILABLE     (1u << 9)
#define MEL_SENSOR_RESULT_NO_DATA         (1u << 10)
#define MEL_SENSOR_RESULT_NOT_STREAMING   (1u << 11)

static inline bool mel_sensor_failed(Mel_Sensor_Status s) { return (s & MEL_SENSOR_SEVERITY_MASK) == MEL_SENSOR_ERROR; }
static inline bool mel_sensor_warned(Mel_Sensor_Status s) { return (s & MEL_SENSOR_SEVERITY_MASK) == MEL_SENSOR_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Sensor;

#define MEL_SENSOR_NULL             ((Mel_Sensor){ 0 })

#define MEL_SENSOR_SIDE_UNSPECIFIED 0u
#define MEL_SENSOR_SIDE_LEFT        1u
#define MEL_SENSOR_SIDE_RIGHT       2u
#define MEL_SENSOR_SIDE_CENTER      3u

#define MEL_SENSOR_VALID_ACCEL      (1u << 0)
#define MEL_SENSOR_VALID_GYRO       (1u << 1)

typedef struct
{
    f64 timestamp_s;
    f32 accel_mps2[3];
    f32 gyro_radps[3];
    u32 valid_mask;
    u64 sequence;
} Mel_Sensor_Reading;

typedef struct
{
    bool has_accel;
    bool has_gyro;

    f32 accel_min_hz, accel_max_hz;
    f32 gyro_min_hz, gyro_max_hz;

    f32 accel_resolution_mps2;
    f32 gyro_resolution_radps;
    f32 accel_range_mps2;
    f32 gyro_range_radps;

    bool requires_permission;
    bool hardware_timestamps;

    u8  side;
    u64 controller_id;
} Mel_Sensor_Caps;

typedef struct
{
    str8            name;
    Mel_Sensor_Caps caps;
} Mel_Sensor_Descriptor;

typedef struct
{
    Mel_Sensor_Descriptor value;
    Mel_Sensor_Status     status;
} Mel_Sensor_Describe_Result;

typedef struct
{
    Mel_Sensor_Reading value;
    Mel_Sensor_Status  status;
} Mel_Sensor_Read_Result;

typedef struct
{
    Mel_Sensor_Caps   value;
    Mel_Sensor_Status status;
} Mel_Sensor_Rate_Result;

typedef struct
{
    f32           accel_hz;
    f32           gyro_hz;
    Mel_Executor* deliver;
} Mel_Sensor_Stream_Opt;

void mel_sensor_init(const Mel_Alloc* alloc);
void mel_sensor_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_sensor_shutdown(void);

u32 mel_sensor_refresh(void);
u32 mel_sensor_count(void);
u32 mel_sensor_list(Mel_Sensor* out, u32 cap);

Mel_Sensor_Describe_Result mel_sensor_describe(Mel_Sensor s);
bool                       mel_sensor_alive(Mel_Sensor s);
bool                       mel_sensor_equal(Mel_Sensor a, Mel_Sensor b);

Mel_Sensor_Rate_Result mel_sensor_rates(Mel_Sensor s);

Mel_Sensor_Status mel_sensor_start_opt(Mel_Sensor s, Mel_Sensor_Stream_Opt opt);
#define mel_sensor_start(s, ...) mel_sensor_start_opt((s), (Mel_Sensor_Stream_Opt){ __VA_ARGS__ })

Mel_Sensor_Status mel_sensor_stop(Mel_Sensor s);
bool              mel_sensor_streaming(Mel_Sensor s);

Mel_Sensor_Read_Result mel_sensor_read(Mel_Sensor s);

void* mel_sensor_native(Mel_Sensor s);

#ifdef __cplusplus
}
#endif
