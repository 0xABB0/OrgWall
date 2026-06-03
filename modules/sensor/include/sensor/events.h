#pragma once

#include <sensor/sensor.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_SENSOR_FIELD_RATES = 1u << 0,
    MEL_SENSOR_FIELD_STREAMS = 1u << 1,
    MEL_SENSOR_FIELD_PERMISSION = 1u << 2,
    MEL_SENSOR_FIELD_SIDE = 1u << 3,
};

typedef enum
{
    MEL_SENSOR_EVENT_ADDED = 0,
    MEL_SENSOR_EVENT_REMOVED,
    MEL_SENSOR_EVENT_CHANGED,
    MEL_SENSOR_EVENT_SAMPLE,
} Mel_Sensor_Event_Kind;

typedef struct
{
    Mel_Sensor_Event_Kind kind;
    Mel_Sensor            sensor;
    u32                   changed_fields;
    Mel_Sensor_Reading    sample;
} Mel_Sensor_Event;

typedef struct Mel_Executor Mel_Executor;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Sensor_Subscription;

#define MEL_SENSOR_SUBSCRIPTION_NULL ((Mel_Sensor_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Sensor_Event_Callback)(const Mel_Sensor_Event* ev, void* user);

u32 mel_sensor_poll_events(Mel_Sensor_Event* out, u32 cap);

Mel_Sensor_Subscription mel_sensor_subscribe(Mel_Executor* exec, Mel_Sensor_Event_Callback cb, void* user);
void                    mel_sensor_unsubscribe(Mel_Sensor_Subscription sub);

#ifdef __cplusplus
}
#endif
