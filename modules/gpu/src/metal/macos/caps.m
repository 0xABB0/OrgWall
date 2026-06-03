#include "mtl_backend.h"

#include <power/power.h>
#include <thermal/thermal.h>

#include <string.h>

void mel_gpu__caps_probe(id<MTLDevice> mtl, Mel_Gpu_Caps* out)
{
    *out = (Mel_Gpu_Caps){ 0 };

    Mel_Gpu_Caps_Adapter* a = &out->adapter;
    const char*           name = mtl.name.UTF8String;
    if (name)
    {
        usize n = strlen(name);
        if (n >= sizeof a->name)
            n = sizeof a->name - 1;
        memcpy(a->name, name, n);
        a->name[n] = 0;
    }
    a->adapter_type = mtl.lowPower ? MEL_GPU_ADAPTER_INTEGRATED : (mtl.hasUnifiedMemory ? MEL_GPU_ADAPTER_INTEGRATED : MEL_GPU_ADAPTER_DISCRETE);
    a->vendor_id = (u32)mtl.registryID;
    a->has_luid = false;
    if (@available(macOS 10.13, *))
    {
        NSUUID* uuid = nil;
        (void)uuid;
    }

    Mel_Gpu_Caps_Memory* m = &out->memory;
    m->host_visible_device_local = mtl.hasUnifiedMemory ? MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_FULL_UMA : MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_NONE;
    m->persistent_map = true;
    m->residency_control = MEL_GPU_RESIDENCY_NONE;
    m->device_local_bytes = (u64)mtl.recommendedMaxWorkingSetSize;
    m->host_visible_bytes = mtl.hasUnifiedMemory ? (u64)mtl.recommendedMaxWorkingSetSize : 0;
    m->bindless.tier = MEL_GPU_TIER_NONE;
    m->bindless.binding_model = MEL_GPU_BINDING_MODEL_DESCRIPTOR_TABLES;

    Mel_Gpu_Caps_Queues* q = &out->queues;
    q->timeline = MEL_GPU_TIMELINE_NATIVE;
    q->internally_synchronized_queues = MEL_GPU_INTERNAL_SYNC_NONE;
    q->async_compute = true;
    q->dedicated_transfer = true;
    q->dedicated_compute = true;

    Mel_Gpu_Caps_Shader* s = &out->shader;
    s->fp16 = true;
    s->int16 = true;
    s->int8 = true;
    s->int64 = false;
    s->fp64 = false;
    s->wave_ops = true;
    s->subgroup_size_min = 32;
    s->subgroup_size_max = 32;

    out->sampler.anisotropy = true;
    out->sampler.max_anisotropy = 16.0f;

    out->raster.fill_mode_non_solid = true;
    out->raster.tile_local = MEL_GPU_TILE_LOCAL_NATIVE;

    out->presentation.allow_tearing = true;
    out->presentation.vrr = false;
    out->presentation.present_wait = MEL_GPU_PRESENT_WAIT_NONE;
    out->presentation.present_timing_feedback = MEL_GPU_PRESENT_TIMING_NONE;

    out->queries.timestamp = MEL_GPU_TIMESTAMP_NONE;
    out->queries.timestamp_calibrated = MEL_GPU_TIMESTAMP_CALIBRATED_NONE;

    out->debug.capture_replay = MEL_GPU_CAPTURE_REPLAY_NONE;

    out->power.power_source = (Mel_Gpu_Power_Source)mel_power_source_current();
    Mel_Thermal_Pressure tp = mel_thermal_current();
    out->power.thermal_pressure = tp > MEL_THERMAL_UNKNOWN ? (Mel_Gpu_Thermal_Tier)(tp - 1) : MEL_GPU_THERMAL_NOMINAL;
    out->power.low_power_mode = mel_power_low_power_current() == MEL_POWER_LOW_POWER_ON;
}
