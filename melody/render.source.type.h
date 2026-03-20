#pragma once

#include "string.str8.h"
#include "render.manager.fwd.h"
#include "render.scene.fwd.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_Source Mel_Render_Source;
typedef struct Mel_Render_Source_Type Mel_Render_Source_Type;
typedef struct Mel_Scene_Forward_Emitter Mel_Scene_Forward_Emitter;
typedef struct Mel_Render_Instance Mel_Render_Instance;

struct Mel_Render_Source_Type {
    str8  name;
    void  (*sync)(Mel_Render_Source* self, Mel_Render_Manager* mgr);
    void  (*scene_forward_emit)(Mel_Render_Source* self,
                                Mel_Render_Handle h,
                                const Mel_Render_Instance* instance,
                                Mel_Scene_Forward_Emitter* emitter);
    void  (*shutdown)(Mel_Render_Source* self);
    usize instance_size;
};

struct Mel_Render_Source {
    const Mel_Render_Source_Type* type;
    void* instance;
    Mel_Render_Scene* scene;
    const Mel_Alloc* alloc;
};

typedef struct {
    const Mel_Render_Source_Type* type;
    const Mel_Alloc* alloc;
} Mel_Render_Source_Create_Opt;

Mel_Render_Source* mel_render_source_create_opt(Mel_Render_Source_Create_Opt opt);
#define mel_render_source_create(...) mel_render_source_create_opt((Mel_Render_Source_Create_Opt){__VA_ARGS__})

void mel_render_source_destroy(Mel_Render_Source* source);

void mel_render_source_sync(Mel_Render_Source* source, Mel_Render_Manager* mgr);

void* mel_render_source_instance(Mel_Render_Source* source);
