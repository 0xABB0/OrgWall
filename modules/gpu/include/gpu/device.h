#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <gpu/status.h>
#include <gpu/caps.h>
#include <gpu/future.h>

typedef struct Mel_Vat Mel_Vat;

typedef struct Mel_Gpu_Instance Mel_Gpu_Instance;
typedef struct Mel_Gpu_Adapter  Mel_Gpu_Adapter;
typedef struct Mel_Gpu_Device   Mel_Gpu_Device;

typedef enum
{
    MEL_GPU_GPU_ASSISTED_OFF = 0,
    MEL_GPU_GPU_ASSISTED_DESCRIPTOR_INDEXING,
    MEL_GPU_GPU_ASSISTED_FULL,
} Mel_Gpu_Gpu_Assisted;

typedef struct
{
    bool                 enabled;
    Mel_Gpu_Gpu_Assisted gpu_assisted;
    bool                 sync_validation;
    bool                 best_practices;
    bool                 debug_printf;
    bool                 thread_safety_tracker;
} Mel_Gpu_Debug_Config;

typedef enum
{
    MEL_GPU_POWER_PREFERENCE_DEFAULT = 0,
    MEL_GPU_POWER_PREFERENCE_LOW,
    MEL_GPU_POWER_PREFERENCE_HIGH,
} Mel_Gpu_Power_Preference;

typedef struct
{
    const char*          app_name;
    Mel_Gpu_Debug_Config debug;
    const Mel_Alloc*     alloc;
} Mel_Gpu_Instance_Opt;

Mel_Gpu_Instance* mel_gpu_instance_create_opt(Mel_Gpu_Instance_Opt opt);
#define mel_gpu_instance_create(...) mel_gpu_instance_create_opt((Mel_Gpu_Instance_Opt){ __VA_ARGS__ })
void mel_gpu_instance_destroy(Mel_Gpu_Instance* inst);

u32          mel_gpu_adapters(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter** out, u32 max);
Mel_Gpu_Caps mel_gpu_adapter_caps(Mel_Gpu_Adapter* adapter);

typedef enum
{
    MEL_GPU_DEVICE_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_DEVICE_CREATE_DEGRADED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_WARNED),
    MEL_GPU_DEVICE_CREATE_NO_ADAPTER = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_DEVICE_CREATE_NO_GRAPHICS_QUEUE = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_DEVICE_CREATE_BACKEND_FAILED = MEL_GPU_STATUS(4, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_DEVICE_CREATE_OOM = MEL_GPU_STATUS(5, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Device_Create_Status;

typedef void (*Mel_Gpu_Device_Lost_Fn)(Mel_Gpu_Device* dev, const char* reason, void* user);

typedef struct
{
    Mel_Vat*                 vat;
    Mel_Gpu_Feature_Request  features;
    Mel_Gpu_Debug_Config     debug;
    const Mel_Alloc*         alloc;
    Mel_Gpu_Power_Preference power_preference;
    Mel_Gpu_Device_Lost_Fn   on_device_lost;
    void*                    device_lost_user;
} Mel_Gpu_Device_Opt;

typedef struct
{
    Mel_Gpu_Device*              value;
    Mel_Gpu_Device_Create_Status status;
} Mel_Gpu_Device_Create_Result;

Mel_Gpu_Device_Create_Result mel_gpu_device_create_opt(Mel_Gpu_Instance* inst, Mel_Gpu_Adapter* adapter, Mel_Gpu_Device_Opt opt);
#define mel_gpu_device_create(inst, adapter, ...) mel_gpu_device_create_opt((inst), (adapter), (Mel_Gpu_Device_Opt){ __VA_ARGS__ })
void mel_gpu_device_destroy(Mel_Gpu_Device* dev);

const Mel_Gpu_Caps* mel_gpu_device_caps(Mel_Gpu_Device* dev);
Mel_Vat*            mel_gpu_device_vat(Mel_Gpu_Device* dev);

typedef struct
{
    Mel_Vat*                 vat;
    Mel_Gpu_Feature_Request  features;
    Mel_Gpu_Debug_Config     debug;
    Mel_Gpu_Power_Preference power_preference;
    const char*              app_name;
} Mel_Gpu_Device_Default_Opt;

Mel_Gpu_Future* mel_gpu_device_create_default_opt(Mel_Gpu_Device_Default_Opt opt);
#define mel_gpu_device_create_default(...) mel_gpu_device_create_default_opt((Mel_Gpu_Device_Default_Opt){ __VA_ARGS__ })
