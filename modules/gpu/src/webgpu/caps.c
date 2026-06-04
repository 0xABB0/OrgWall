#include "wgpu_backend.h"

#include <power/power.h>
#include <thermal/thermal.h>

#include <string.h>

static void mel_gpu__copy_sv(char* out, usize cap, WGPUStringView sv)
{
    usize n = sv.data ? sv.length : 0;
    if (n >= cap)
        n = cap - 1;
    if (n)
        memcpy(out, sv.data, n);
    out[n] = 0;
}

static Mel_Gpu_Adapter_Type mel_gpu__adapter_type(WGPUAdapterType t)
{
    switch (t)
    {
    case WGPUAdapterType_DiscreteGPU:
        return MEL_GPU_ADAPTER_DISCRETE;
    case WGPUAdapterType_IntegratedGPU:
        return MEL_GPU_ADAPTER_INTEGRATED;
    case WGPUAdapterType_CPU:
        return MEL_GPU_ADAPTER_SOFTWARE;
    default:
        return MEL_GPU_ADAPTER_VIRTUAL;
    }
}

void mel_gpu__caps_probe(WGPUAdapter adapter, Mel_Gpu_Caps* out)
{
    *out = (Mel_Gpu_Caps){ 0 };

    WGPUAdapterInfo info = { 0 };
    bool            have_info = wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success;

    WGPULimits limits = { 0 };
    bool       have_limits = wgpuAdapterGetLimits(adapter, &limits) == WGPUStatus_Success;

    Mel_Gpu_Caps_Adapter* a = &out->adapter;
    if (have_info)
    {
        if (info.device.data && info.device.length)
            mel_gpu__copy_sv(a->name, sizeof a->name, info.device);
        else
            mel_gpu__copy_sv(a->name, sizeof a->name, info.description);
        a->adapter_type = mel_gpu__adapter_type(info.adapterType);
        a->vendor_id = info.vendorID;
        a->device_id = info.deviceID;
    }
    if (a->name[0] == 0)
        memcpy(a->name, "WebGPU Adapter", sizeof "WebGPU Adapter");
    a->has_luid = false;

    Mel_Gpu_Caps_Memory* m = &out->memory;
    /* Dawn-on-Metal Apple silicon is UMA; WebGPU core never exposes a host-visible
       VRAM window as a separate tier, so report full_uma when the adapter is integrated. */
    m->host_visible_device_local = a->adapter_type == MEL_GPU_ADAPTER_INTEGRATED ? MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_FULL_UMA : MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_NONE;
    m->persistent_map = false;
    m->residency_control = MEL_GPU_RESIDENCY_NONE;
    m->sparse_buffer = false;
    m->sparse_texture = false;
    m->device_local_bytes = have_limits ? limits.maxBufferSize : 0;
    m->host_visible_bytes = 0;

    /* WebGPU core has no true bindless heap; sized binding arrays are the shipping
       stepping-stone (spec §2 / §3.4). Report capped, never full. */
    m->bindless.tier = MEL_GPU_TIER_CAPPED;
    m->bindless.binding_model = MEL_GPU_BINDING_MODEL_DESCRIPTOR_TABLES;
    m->bindless.max_texture_view_slots = have_limits ? limits.maxSampledTexturesPerShaderStage : 0;
    m->bindless.max_sampler_slots = have_limits ? limits.maxSamplersPerShaderStage : 0;
    m->bindless.max_storage_buffer_slots = have_limits ? limits.maxStorageBuffersPerShaderStage : 0;
    m->bindless.max_uniform_buffer_slots = have_limits ? limits.maxUniformBuffersPerShaderStage : 0;
    m->bindless.max_storage_image_slots = have_limits ? limits.maxStorageTexturesPerShaderStage : 0;

    Mel_Gpu_Caps_Queues* q = &out->queues;
    /* Single implicit queue: timeline ordering is emulated against the device queue. */
    q->timeline = MEL_GPU_TIMELINE_EMULATED;
    q->internally_synchronized_queues = MEL_GPU_INTERNAL_SYNC_NONE;
    q->async_compute = false;
    q->dedicated_transfer = false;
    q->dedicated_compute = false;

    Mel_Gpu_Caps_Shader* s = &out->shader;
    s->fp16 = wgpuAdapterHasFeature(adapter, WGPUFeatureName_ShaderF16);
    s->fp64 = false;
    s->int16 = false;
    s->int64 = false;
    s->int8 = false;
    s->wave_ops = wgpuAdapterHasFeature(adapter, WGPUFeatureName_Subgroups);
    s->draw_parameters = false;
    s->subgroup_size_min = have_info ? info.subgroupMinSize : 0;
    s->subgroup_size_max = have_info ? info.subgroupMaxSize : 0;
    s->bytecode_passthrough.wgsl = true;
    /* The vendored Dawn Release prebuilt has Tint's SPIR-V reader compiled out, so
       WGPUShaderSourceSPIRV is rejected at runtime ("SPIR-V is disallowed"). Report it
       honestly absent (MEL-ENGINE-VIII); WGSL is the supported path on this build. */
    s->bytecode_passthrough.spirv = false;
    s->bytecode_passthrough.msl = false;
    s->bytecode_passthrough.dxil = false;

    out->sampler.anisotropy = true;
    out->sampler.max_anisotropy = 16.0f;

    out->raster.fill_mode_non_solid = false;
    /* No subpass-input / pixel-local-storage in WebGPU core; tile-local is emulated. */
    out->raster.tile_local = MEL_GPU_TILE_LOCAL_EMULATED;

    Mel_Gpu_Caps_Features* feat = &out->features;
    feat->ray_tracing = MEL_GPU_RT_NONE;
    feat->video_decode = MEL_GPU_VIDEO_NONE;
    feat->video_encode = MEL_GPU_VIDEO_NONE;
    feat->video_process = MEL_GPU_VIDEO_NONE;
    feat->asset_io = MEL_GPU_ASSET_IO_NONE;
    feat->mesh_shaders = false;
    feat->work_graphs = false;
    feat->ml_tensor = false;

    out->presentation.allow_tearing = false;
    out->presentation.vrr = false;
    out->presentation.present_wait = MEL_GPU_PRESENT_WAIT_NONE;
    out->presentation.present_timing_feedback = MEL_GPU_PRESENT_TIMING_NONE;

    out->queries.timestamp = wgpuAdapterHasFeature(adapter, WGPUFeatureName_TimestampQuery) ? MEL_GPU_TIMESTAMP_QUANTIZED_100US : MEL_GPU_TIMESTAMP_NONE;
    out->queries.timestamp_calibrated = MEL_GPU_TIMESTAMP_CALIBRATED_NONE;

    out->debug.capture_replay = MEL_GPU_CAPTURE_REPLAY_NONE;

    out->power.power_source = (Mel_Gpu_Power_Source)mel_power_source_current();
    Mel_Thermal_Pressure tp = mel_thermal_current();
    out->power.thermal_pressure = tp > MEL_THERMAL_UNKNOWN ? (Mel_Gpu_Thermal_Tier)(tp - 1) : MEL_GPU_THERMAL_NOMINAL;
    out->power.low_power_mode = mel_power_low_power_current() == MEL_POWER_LOW_POWER_ON;
}
