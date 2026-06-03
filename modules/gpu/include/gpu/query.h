#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/future.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;
typedef struct Mel_Gpu_Queue        Mel_Gpu_Queue;

MEL_GPU_HANDLE(Mel_Gpu_Query_Pool);

typedef enum
{
    MEL_GPU_QUERY_TIMESTAMP = 0,
} Mel_Gpu_Query_Type;

typedef enum
{
    MEL_GPU_QUERY_POOL_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_QUERY_POOL_CREATE_UNSUPPORTED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_QUERY_POOL_CREATE_BAD_PARAMS = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_QUERY_POOL_CREATE_VK_FAILED = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Query_Pool_Create_Status;

typedef struct
{
    Mel_Gpu_Query_Type type;
    u32                count;
    const char*        name;
} Mel_Gpu_Query_Pool_Opt;

typedef struct
{
    Mel_Gpu_Query_Pool               value;
    Mel_Gpu_Query_Pool_Create_Status status;
} Mel_Gpu_Query_Pool_Create_Result;

Mel_Gpu_Query_Pool_Create_Result mel_gpu_query_pool_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool_Opt opt);
#define mel_gpu_query_pool_create(dev, ...) mel_gpu_query_pool_create_opt((dev), (Mel_Gpu_Query_Pool_Opt){ __VA_ARGS__ })

void mel_gpu_query_pool_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool);
bool mel_gpu_query_pool_alive(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool);

void mel_gpu_cmd_reset_query_pool(Mel_Gpu_Command_List* cmd, Mel_Gpu_Query_Pool pool, u32 first, u32 count);
void mel_gpu_cmd_write_timestamp(Mel_Gpu_Command_List* cmd, Mel_Gpu_Query_Pool pool, u32 index);

bool mel_gpu_query_pool_resolve(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool, u32 first, u32 count, u64* out_ns);

typedef enum
{
    MEL_GPU_QUERY_RESOLVE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_QUERY_RESOLVE_BAD_PARAMS = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_QUERY_RESOLVE_VK_FAILED = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Query_Resolve_Status;

typedef struct
{
    const u64* ns;
    u32        count;
} Mel_Gpu_Query_Resolve;

Mel_Gpu_Future* mel_gpu_query_pool_resolve_async(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Query_Pool pool, u32 first, u32 count);
void            mel_gpu_query_resolve_future_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Future* f);
