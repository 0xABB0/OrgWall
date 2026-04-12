#pragma once

#include "gpu.buffer.h"
#include "gpu.cmd.fwd.h"
#include "collection.array.h"

typedef struct Mel_Indirect_Draw Mel_Indirect_Draw;

typedef struct {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
} Mel_Draw_Indexed_Indirect_Cmd;

typedef struct {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
} Mel_Draw_Mesh_Tasks_Indirect_Cmd;

struct Mel_Indirect_Draw {
    Mel_Gpu_Buffer buffer;
    Mel_Array(u8) commands;
    usize stride;
    u32 count;
    bool gpu_writable;
};

typedef struct {
    Mel_Gpu_Device* dev;
    usize stride;
    u32 initial_capacity;
    bool gpu_writable;
} Mel_Indirect_Draw_Opt;

void mel_indirect_init_opt(Mel_Indirect_Draw* ind, Mel_Indirect_Draw_Opt opt);
#define mel_indirect_init(ind, ...) mel_indirect_init_opt((ind), (Mel_Indirect_Draw_Opt){__VA_ARGS__})

void mel_indirect_shutdown(Mel_Indirect_Draw* ind, Mel_Gpu_Device* dev);

void mel_indirect_append(Mel_Indirect_Draw* ind, const void* cmd);
void mel_indirect_clear(Mel_Indirect_Draw* ind);
u32  mel_indirect_count(Mel_Indirect_Draw* ind);

void mel_indirect_upload(Mel_Indirect_Draw* ind, Mel_Gpu_Device* dev);
void mel_indirect_bind(Mel_Indirect_Draw* ind, Mel_Gpu_Cmd* c);
