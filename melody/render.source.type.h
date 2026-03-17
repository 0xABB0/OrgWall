#pragma once

#include "string.str8.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_Source Mel_Render_Source;
typedef struct Mel_Render_Source_Type Mel_Render_Source_Type;

struct Mel_Render_Source_Type {
    str8  name;
    void* (*create_manager)(Mel_Render_Source* self, Mel_Gpu_Device* dev, const Mel_Alloc* alloc);
    void  (*destroy_manager)(Mel_Render_Source* self, void* mgr);
    void  (*sync)(Mel_Render_Source* self, void* mgr);
    void  (*shutdown)(Mel_Render_Source* self);
    usize instance_size;
};

struct Mel_Render_Source {
    const Mel_Render_Source_Type* type;
    void* instance;
    void* manager;
};

typedef struct {
    const Mel_Render_Source_Type* type;
    const Mel_Alloc* alloc;
} Mel_Render_Source_Create_Opt;

Mel_Render_Source* mel_render_source_create_opt(Mel_Render_Source_Create_Opt opt);
#define mel_render_source_create(...) mel_render_source_create_opt((Mel_Render_Source_Create_Opt){__VA_ARGS__})

void mel_render_source_destroy(Mel_Render_Source* source);

void mel_render_source_sync(Mel_Render_Source* source);

void* mel_render_source_manager(Mel_Render_Source* source);
void* mel_render_source_instance(Mel_Render_Source* source);

void mel__render_source_ensure_manager(Mel_Render_Source* source, Mel_Gpu_Device* dev, const Mel_Alloc* alloc);
