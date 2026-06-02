#pragma once

#include <core/types.h>

typedef enum
{
    MEL_GPU_ADAPTER_DISCRETE = 0,
    MEL_GPU_ADAPTER_INTEGRATED,
    MEL_GPU_ADAPTER_SOFTWARE,
    MEL_GPU_ADAPTER_VIRTUAL,
    MEL_GPU_ADAPTER_EXTERNAL,
} Mel_Gpu_Adapter_Type;

typedef enum
{
    MEL_GPU_TIER_NONE = 0,
    MEL_GPU_TIER_CAPPED,
    MEL_GPU_TIER_FULL,
} Mel_Gpu_Bindless_Tier;

typedef enum
{
    MEL_GPU_TIMELINE_EMULATED = 0,
    MEL_GPU_TIMELINE_NATIVE,
} Mel_Gpu_Timeline_Tier;

typedef enum
{
    MEL_GPU_RT_NONE = 0,
    MEL_GPU_RT_INLINE,
    MEL_GPU_RT_PIPELINE,
} Mel_Gpu_Ray_Tracing_Tier;

typedef enum
{
    MEL_GPU_VIDEO_NONE = 0,
    MEL_GPU_VIDEO_IMPORT_ONLY,
    MEL_GPU_VIDEO_HARDWARE,
} Mel_Gpu_Video_Tier;

typedef enum
{
    MEL_GPU_ASSET_IO_NONE = 0,
    MEL_GPU_ASSET_IO_CPU_STAGED,
    MEL_GPU_ASSET_IO_GPU_DECOMPRESS,
} Mel_Gpu_Asset_Io_Tier;

typedef enum
{
    MEL_GPU_RESIDENCY_NONE = 0,
    MEL_GPU_RESIDENCY_BUDGET_ONLY,
    MEL_GPU_RESIDENCY_EXPLICIT,
} Mel_Gpu_Residency_Tier;

typedef enum
{
    MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_NONE = 0,
    MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_REBAR,
    MEL_GPU_HOST_VISIBLE_DEVICE_LOCAL_FULL_UMA,
} Mel_Gpu_Host_Visible_Device_Local;

typedef enum
{
    MEL_GPU_TIMESTAMP_NONE = 0,
    MEL_GPU_TIMESTAMP_QUANTIZED_100US,
    MEL_GPU_TIMESTAMP_NATIVE,
} Mel_Gpu_Timestamp_Tier;

typedef enum
{
    MEL_GPU_TIMESTAMP_CALIBRATED_NONE = 0,
    MEL_GPU_TIMESTAMP_CALIBRATED_PRESENTATION_ONLY,
    MEL_GPU_TIMESTAMP_CALIBRATED_CPU_GPU_PAIR,
} Mel_Gpu_Timestamp_Calibrated_Tier;

typedef enum
{
    MEL_GPU_PRESENT_WAIT_NONE = 0,
    MEL_GPU_PRESENT_WAIT_PRESENT_ID,
    MEL_GPU_PRESENT_WAIT_PRESENT_ID_WAIT,
} Mel_Gpu_Present_Wait_Tier;

typedef enum
{
    MEL_GPU_PRESENT_TIMING_NONE = 0,
    MEL_GPU_PRESENT_TIMING_COMPLETION_ONLY,
    MEL_GPU_PRESENT_TIMING_TIMESTAMPS,
    MEL_GPU_PRESENT_TIMING_SCHEDULED_PRESENT,
} Mel_Gpu_Present_Timing_Tier;

typedef enum
{
    MEL_GPU_INTERNAL_SYNC_NONE = 0,
    MEL_GPU_INTERNAL_SYNC_PARTIAL,
    MEL_GPU_INTERNAL_SYNC_FULL,
} Mel_Gpu_Internal_Sync_Tier;

typedef enum
{
    MEL_GPU_CAPTURE_REPLAY_NONE = 0,
    MEL_GPU_CAPTURE_REPLAY_PARTIAL,
    MEL_GPU_CAPTURE_REPLAY_FULL,
} Mel_Gpu_Capture_Replay_Tier;

typedef enum
{
    MEL_GPU_POWER_SOURCE_UNKNOWN = 0,
    MEL_GPU_POWER_SOURCE_AC,
    MEL_GPU_POWER_SOURCE_BATTERY,
} Mel_Gpu_Power_Source;

typedef enum
{
    MEL_GPU_THERMAL_NOMINAL = 0,
    MEL_GPU_THERMAL_FAIR,
    MEL_GPU_THERMAL_SERIOUS,
    MEL_GPU_THERMAL_CRITICAL,
} Mel_Gpu_Thermal_Tier;

typedef enum
{
    MEL_GPU_TILE_LOCAL_NONE = 0,
    MEL_GPU_TILE_LOCAL_EMULATED,
    MEL_GPU_TILE_LOCAL_NATIVE,
} Mel_Gpu_Tile_Local_Tier;

typedef struct
{
    Mel_Gpu_Adapter_Type adapter_type;
    u32                  vendor_id;
    u32                  device_id;
    u32                  driver_version;
    u8                   uuid[16];
    u64                  luid;
    bool                 has_luid;
    char                 name[256];
} Mel_Gpu_Caps_Adapter;

typedef enum
{
    MEL_GPU_BINDING_MODEL_DESCRIPTOR_TABLES = 0,
    MEL_GPU_BINDING_MODEL_ROOT_RECORD,
} Mel_Gpu_Binding_Model;

typedef enum
{
    MEL_GPU_ROOT_RECORD_PAYLOAD_DESCRIPTOR_INDICES = 0,
    MEL_GPU_ROOT_RECORD_PAYLOAD_POINTERS,
    MEL_GPU_ROOT_RECORD_PAYLOAD_MIXED,
} Mel_Gpu_Root_Record_Payload;

typedef enum
{
    MEL_GPU_ROOT_RECORD_UPDATE_STAGING_COPY = 0,
    MEL_GPU_ROOT_RECORD_UPDATE_UPLOAD_RING,
    MEL_GPU_ROOT_RECORD_UPDATE_PERSISTENT_MAP,
    MEL_GPU_ROOT_RECORD_UPDATE_GPU_GENERATED,
} Mel_Gpu_Root_Record_Update;

typedef struct
{
    Mel_Gpu_Bindless_Tier       tier;
    Mel_Gpu_Binding_Model       binding_model;
    Mel_Gpu_Root_Record_Payload root_record_payload;
    Mel_Gpu_Root_Record_Update  root_record_update;
    u32                         max_texture_view_slots;
    u32                         max_sampler_slots;
    u32                         max_storage_buffer_slots;
    u32                         max_uniform_buffer_slots;
    u32                         max_storage_image_slots;
} Mel_Gpu_Caps_Bindless;

typedef struct
{
    Mel_Gpu_Host_Visible_Device_Local host_visible_device_local;
    Mel_Gpu_Residency_Tier            residency_control;
    Mel_Gpu_Caps_Bindless             bindless;
    bool                              persistent_map;
    bool                              sparse_buffer;
    bool                              sparse_texture;
    u64                               device_local_bytes;
    u64                               host_visible_bytes;
} Mel_Gpu_Caps_Memory;

typedef struct
{
    Mel_Gpu_Internal_Sync_Tier internally_synchronized_queues;
    Mel_Gpu_Timeline_Tier      timeline;
    bool                       async_compute;
    bool                       dedicated_transfer;
    bool                       dedicated_compute;
} Mel_Gpu_Caps_Queues;

typedef struct
{
    bool fp16;
    bool fp64;
    bool int16;
    bool int64;
    bool int8;
    bool wave_ops;
    u32  subgroup_size_min;
    u32  subgroup_size_max;
} Mel_Gpu_Caps_Shader;

typedef struct
{
    Mel_Gpu_Ray_Tracing_Tier ray_tracing;
    Mel_Gpu_Video_Tier       video_decode;
    Mel_Gpu_Video_Tier       video_encode;
    Mel_Gpu_Video_Tier       video_process;
    Mel_Gpu_Asset_Io_Tier    asset_io;
    bool                     mesh_shaders;
    bool                     work_graphs;
    bool                     ml_tensor;
} Mel_Gpu_Caps_Features;

typedef struct
{
    bool                        allow_tearing;
    bool                        vrr;
    bool                        frame_latency_waitable;
    bool                        shared_presentable_image;
    bool                        pre_rotation;
    Mel_Gpu_Present_Wait_Tier   present_wait;
    Mel_Gpu_Present_Timing_Tier present_timing_feedback;
} Mel_Gpu_Caps_Presentation;

typedef struct
{
    Mel_Gpu_Timestamp_Tier            timestamp;
    Mel_Gpu_Timestamp_Calibrated_Tier timestamp_calibrated;
    bool                              timestamp_compute_and_graphics;
    bool                              occlusion_precise;
    bool                              pipeline_statistics;
    f64                               timestamp_period_ns;
} Mel_Gpu_Caps_Queries;

typedef struct
{
    Mel_Gpu_Power_Source power_source;
    Mel_Gpu_Thermal_Tier thermal_pressure;
    bool                 low_power_mode;
} Mel_Gpu_Caps_Power;

typedef struct
{
    Mel_Gpu_Capture_Replay_Tier capture_replay;
    bool                        validation_available;
} Mel_Gpu_Caps_Debug;

typedef struct
{
    Mel_Gpu_Tile_Local_Tier tile_local;
} Mel_Gpu_Caps_Raster;

typedef struct
{
    bool anisotropy;
    f32  max_anisotropy;
} Mel_Gpu_Caps_Sampler;

typedef struct
{
    Mel_Gpu_Caps_Adapter      adapter;
    Mel_Gpu_Caps_Memory       memory;
    Mel_Gpu_Caps_Queues       queues;
    Mel_Gpu_Caps_Shader       shader;
    Mel_Gpu_Caps_Features     features;
    Mel_Gpu_Caps_Presentation presentation;
    Mel_Gpu_Caps_Queries      queries;
    Mel_Gpu_Caps_Power        power;
    Mel_Gpu_Caps_Debug        debug;
    Mel_Gpu_Caps_Raster       raster;
    Mel_Gpu_Caps_Sampler      sampler;
} Mel_Gpu_Caps;

typedef struct
{
    bool ray_tracing;
    bool mesh_shaders;
    bool timeline_semaphores;
    bool buffer_device_address;
    bool descriptor_indexing;
    bool internally_synchronized_queues;
    bool capture_replay;
    bool persistent_map;
    bool host_image_copy;
} Mel_Gpu_Feature_Request;
