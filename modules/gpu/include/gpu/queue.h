#pragma once

#include <core/types.h>
#include <gpu/status.h>
#include <gpu/future.h>
#include <gpu/sync.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Queue        Mel_Gpu_Queue;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

typedef enum
{
    MEL_GPU_QUEUE_GRAPHICS = 0,
    MEL_GPU_QUEUE_COMPUTE,
    MEL_GPU_QUEUE_TRANSFER,
    MEL_GPU_QUEUE_ASYNC_COMPUTE,
    MEL_GPU_QUEUE_VIDEO_DECODE,
    MEL_GPU_QUEUE_VIDEO_ENCODE,
    MEL_GPU_QUEUE_VIDEO_PROCESS,
    MEL_GPU_QUEUE_SPARSE_BINDING,
    MEL_GPU_QUEUE_ASSET_IO,
} Mel_Gpu_Queue_Role;

typedef enum
{
    MEL_GPU_QUEUE_PRIORITY_LOW = 0,
    MEL_GPU_QUEUE_PRIORITY_NORMAL,
    MEL_GPU_QUEUE_PRIORITY_HIGH,
    MEL_GPU_QUEUE_PRIORITY_REALTIME,
} Mel_Gpu_Queue_Priority;

typedef struct
{
    Mel_Gpu_Queue_Priority priority;
    bool                   dedicated;
    bool                   internally_synchronized;
    bool                   allow_locked_fallback;
} Mel_Gpu_Queue_Request_Opt;

typedef struct
{
    u32  family_index;
    bool supports_graphics;
    bool supports_compute;
    bool supports_transfer;
    bool supports_sparse_binding;
    u32  timestamp_valid_bits;
} Mel_Gpu_Queue_Info;

u32            mel_gpu_queue_available(Mel_Gpu_Device* dev, Mel_Gpu_Queue_Role role, Mel_Gpu_Queue_Priority priority);
Mel_Gpu_Queue* mel_gpu_queue_request_opt(Mel_Gpu_Device* dev, Mel_Gpu_Queue_Role role, Mel_Gpu_Queue_Request_Opt opt);
#define mel_gpu_queue_request(dev, role, ...) mel_gpu_queue_request_opt((dev), (role), (Mel_Gpu_Queue_Request_Opt){ __VA_ARGS__ })
void               mel_gpu_queue_release(Mel_Gpu_Queue* q);
Mel_Gpu_Queue_Info mel_gpu_queue_info(Mel_Gpu_Queue* q);

typedef struct
{
    Mel_Gpu_Sync sync;
    u64          value;
} Mel_Gpu_Submit_Sync;

typedef struct
{
    Mel_Gpu_Command_List* const* command_lists;
    u32                          command_list_count;
    const Mel_Gpu_Submit_Sync*   wait;
    u32                          wait_count;
    const Mel_Gpu_Submit_Sync*   signal;
    u32                          signal_count;
} Mel_Gpu_Submit;

Mel_Gpu_Future* mel_gpu_queue_submit(Mel_Gpu_Queue* q, Mel_Gpu_Submit submit);
