#pragma once

#include "string.str8.h"
#include "render.manager.fwd.h"
#include "allocator.fwd.h"

typedef struct Mel_Render_View Mel_Render_View;
typedef struct Mel_Render_Pipeline Mel_Render_Pipeline;
typedef struct Mel_Render_Pipeline_Type Mel_Render_Pipeline_Type;

struct Mel_Render_Pipeline_Type {
    str8  name;
    void  (*init)(Mel_Render_Pipeline* self, Mel_Render_View* view);
    void  (*draw)(Mel_Render_Pipeline* self, Mel_Render_Manager* mgr);
    void  (*shutdown)(Mel_Render_Pipeline* self);
    usize instance_size;
};

struct Mel_Render_Pipeline {
    const Mel_Render_Pipeline_Type* type;
    void* instance;
    Mel_Render_View* view;
};

void mel_pipeline_register(const Mel_Render_Pipeline_Type* type);
const Mel_Render_Pipeline_Type* mel_pipeline_find(str8 name);
u32 mel_pipeline_registered_count(void);

Mel_Render_Pipeline* mel_pipeline_create(const Mel_Render_Pipeline_Type* type,
                                          Mel_Render_View* view,
                                          const Mel_Alloc* alloc);
void mel_pipeline_destroy(Mel_Render_Pipeline* pipeline);
void mel_pipeline_init_frame(Mel_Render_Pipeline* pipeline, Mel_Render_View* view);
void mel_pipeline_draw(Mel_Render_Pipeline* pipeline, Mel_Render_Manager* mgr);
void* mel_pipeline_instance(Mel_Render_Pipeline* pipeline);
