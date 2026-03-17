#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "gpu.device.fwd.h"
#include "gpu.descriptor.h"
#include "gpu.cmd.fwd.h"
#include "collection.bitset.h"

#define MEL_TEXTURE_TABLE_INVALID_INDEX UINT32_MAX

typedef struct Mel_Texture_Table Mel_Texture_Table;

struct Mel_Texture_Table {
    Mel_Gpu_Descriptor_Layout layout;
    Mel_Gpu_Descriptor_Pool pool;
    void* _set;
    Mel_BitSet used;
    u32 capacity;
    Mel_Gpu_Device* dev;
};

typedef struct {
    u32 capacity;
} Mel_Texture_Table_Opt;

void mel_texture_table_init_opt(Mel_Texture_Table* tt, Mel_Gpu_Device* dev, const Mel_Alloc* alloc, Mel_Texture_Table_Opt opt);
#define mel_texture_table_init(tt, dev, alloc, ...) mel_texture_table_init_opt((tt), (dev), (alloc), (Mel_Texture_Table_Opt){__VA_ARGS__})

void mel_texture_table_shutdown(Mel_Texture_Table* tt);

u32 mel_texture_table_add(Mel_Texture_Table* tt, void* view, void* sampler);
void mel_texture_table_remove(Mel_Texture_Table* tt, u32 index);
void mel_texture_table_bind(Mel_Texture_Table* tt, Mel_Gpu_Cmd* cmd, void* pipeline_layout, u32 set_index);
u32 mel_texture_table_count(Mel_Texture_Table* tt);
