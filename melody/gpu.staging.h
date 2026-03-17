#pragma once

#include "gpu.buffer.h"
#include "gpu.cmd.fwd.h"
#include "allocator.fwd.h"
#include "allocator.arena.h"

typedef struct Mel_Staging Mel_Staging;

typedef struct {
    Mel_Gpu_Buffer* dst;
    u64 dst_offset;
    u64 src_offset;
    u64 size;
} Mel_Staging_Copy;

struct Mel_Staging {
    Mel_Gpu_Buffer buffer;
    Mel_Arena arena;
    Mel_Staging_Copy* copies;
    u32 copy_count;
    u32 copy_capacity;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    u64 buffer_size;
    const Mel_Alloc* alloc;
} Mel_Staging_Opt;

void mel_staging_init_opt(Mel_Staging* stg, Mel_Staging_Opt opt);
#define mel_staging_init(stg, ...) mel_staging_init_opt((stg), (Mel_Staging_Opt){__VA_ARGS__})

void mel_staging_shutdown(Mel_Staging* stg, Mel_Gpu_Device* dev);

void mel_staging_write(Mel_Staging* stg, Mel_Gpu_Buffer* dst, u64 offset, const void* data, u64 size);
void mel_staging_flush(Mel_Staging* stg, Mel_Gpu_Cmd* c);
void mel_staging_reset(Mel_Staging* stg);
