#pragma once

#include "gpu.image.h"
#include "allocator.fwd.h"

typedef struct Mel_Scratch_Pool Mel_Scratch_Pool;
typedef struct Mel_Scratch_Block Mel_Scratch_Block;
typedef struct Mel_Scratch_Image Mel_Scratch_Image;

struct Mel_Scratch_Image
{
    Mel_Gpu_Image image;
    Mel_Scratch_Block* block;
};

struct Mel_Scratch_Block
{
    void* _memory;
    u64 size;
    Mel_Scratch_Image* images;
    u32 image_count;
    u32 image_capacity;
    u32 active_count;
};

struct Mel_Scratch_Pool
{
    Mel_Gpu_Device* dev;
    Mel_Scratch_Block* blocks;
    u32 block_count;
    u32 block_capacity;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
} Mel_Scratch_Pool_Opt;

void mel_scratch_pool_init_opt(Mel_Scratch_Pool* pool, Mel_Scratch_Pool_Opt opt);
#define mel_scratch_pool_init(pool, ...) mel_scratch_pool_init_opt((pool), (Mel_Scratch_Pool_Opt){__VA_ARGS__})

void mel_scratch_pool_shutdown(Mel_Scratch_Pool* pool);

typedef struct {
    u32 width;
    u32 height;
    Mel_Gpu_Format format;
    Mel_Gpu_Image_Usage usage;
    Mel_Gpu_Aspect aspect;
} Mel_Scratch_Desc;

Mel_Gpu_Image mel_scratch_acquire_opt(Mel_Scratch_Pool* pool, Mel_Scratch_Desc desc);
#define mel_scratch_acquire(pool, ...) mel_scratch_acquire_opt((pool), (Mel_Scratch_Desc){__VA_ARGS__})

void mel_scratch_release(Mel_Scratch_Pool* pool, Mel_Gpu_Image img);

void mel_scratch_pool_reset(Mel_Scratch_Pool* pool);
