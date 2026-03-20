#pragma once

#include "string.str8.h"
#include "render.manager.fwd.h"
#include "gpu.cmd.fwd.h"
#include "gpu.device.fwd.h"
#include "gpu.types.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_View Mel_Render_View;
typedef struct Mel_Render_Target Mel_Render_Target;
typedef struct Mel_Render_Pipeline Mel_Render_Pipeline;
typedef struct Mel_Render_Pipeline_Scene Mel_Render_Pipeline_Scene;
typedef struct Mel_Render_Pipeline_Type Mel_Render_Pipeline_Type;
typedef struct Mel_Render_Draw_Ctx Mel_Render_Draw_Ctx;

struct Mel_Render_Draw_Ctx {
    Mel_Gpu_Cmd* cmd;
    Mel_Render_Target* target;
    u32 target_width;
    u32 target_height;
    Mel_Gpu_Format target_format;
};

struct Mel_Render_Pipeline_Type {
    str8  name;
    void  (*scene_init)(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr, Mel_Gpu_Device* dev);
    void  (*scene_sync)(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr);
    void  (*scene_shutdown)(Mel_Render_Pipeline_Scene* self);
    usize scene_size;
    void  (*view_init)(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene);
    void  (*begin_frame)(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene);
    void  (*draw)(Mel_Render_Pipeline* self, Mel_Render_Pipeline_Scene* scene, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx);
    void  (*view_shutdown)(Mel_Render_Pipeline* self);
    usize instance_size;
};

struct Mel_Render_Pipeline_Scene {
    const Mel_Render_Pipeline_Type* type;
    void* instance;
    Mel_Render_Manager* manager;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

struct Mel_Render_Pipeline {
    const Mel_Render_Pipeline_Type* type;
    void* instance;
    Mel_Render_View* view;
    Mel_Render_Pipeline_Scene* scene;
    const Mel_Alloc* alloc;
};

void mel_pipeline_register(const Mel_Render_Pipeline_Type* type);
const Mel_Render_Pipeline_Type* mel_pipeline_find(str8 name);
u32 mel_pipeline_registered_count(void);

Mel_Render_Pipeline_Scene* mel_pipeline_scene_create(const Mel_Render_Pipeline_Type* type,
                                                     Mel_Render_Manager* mgr,
                                                     Mel_Gpu_Device* dev,
                                                     const Mel_Alloc* alloc);
void mel_pipeline_scene_destroy(Mel_Render_Pipeline_Scene* scene);
void mel_pipeline_scene_sync(Mel_Render_Pipeline_Scene* scene, Mel_Render_Manager* mgr);
void* mel_pipeline_scene_instance(Mel_Render_Pipeline_Scene* scene);

Mel_Render_Pipeline* mel_pipeline_create(const Mel_Render_Pipeline_Type* type,
                                          Mel_Render_View* view,
                                          Mel_Render_Pipeline_Scene* scene,
                                          const Mel_Alloc* alloc);
void mel_pipeline_destroy(Mel_Render_Pipeline* pipeline);
void mel_pipeline_begin_frame(Mel_Render_Pipeline* pipeline, Mel_Render_View* view);
void mel_pipeline_draw(Mel_Render_Pipeline* pipeline, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx);
void* mel_pipeline_instance(Mel_Render_Pipeline* pipeline);
