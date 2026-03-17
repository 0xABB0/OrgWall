#pragma once

#include "render.graph.fwd.h"
#include "render.pass.h"
#include "collection.array.h"
#include "string.str8.fwd.h"
#include "swapchain.fwd.h"
#include "gpu.tracy.fwd.h"

#ifndef MEL_MAX_FRAMES_IN_FLIGHT
#define MEL_MAX_FRAMES_IN_FLIGHT 3
#endif

typedef struct Mel_Render_Graph_Dep Mel_Render_Graph_Dep;

struct Mel_Render_Graph_Pass {
    str8 name;
    Mel_Render_Pass_Fn fn;
    void* user;
    u32 type;
    u32 viewport_mode;
    u32 viewport_design_width;
    u32 viewport_design_height;
    u32 render_width;
    u32 render_height;
    Mel_Render_List** read_lists;
    Mel_Source_Handle* read_sources;
    Mel_Render_List** write_lists;
    Mel_Render_Target** read_targets;
    Mel_Pass_Write_Target* write_targets;
    Mel_Camera* camera;
};

struct Mel_Render_Graph_Barrier {
    u32 pass_index;
    Mel_Render_Target* target;
    Mel_Render_List* list;
    Mel_Gpu_Stage src_stage;
    Mel_Gpu_Access src_access;
    Mel_Gpu_Stage dst_stage;
    Mel_Gpu_Access dst_access;
    Mel_Gpu_Image_Layout old_layout;
    Mel_Gpu_Image_Layout new_layout;
    Mel_Gpu_Aspect aspect;
};

struct Mel_Render_Graph_Dep {
    u32 from;
    u32 to;
};

typedef struct {
    void* _pool;
    void* _cmd;
    void* _fence;
} Mel_Render_Graph_Frame;

struct Mel_Render_Graph {
    Mel_Array(Mel_Render_Graph_Pass) passes;
    Mel_Array(u32) sorted_order;
    Mel_Array(Mel_Render_Graph_Barrier) barriers;
    Mel_Array(Mel_Render_Graph_Dep) explicit_deps;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    bool dirty;

    Mel_Render_Graph_Frame frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 frame_count;
    u32 current_frame;
    u64 execute_count;
    Mel_Gpu_Tracy_Ctx* tracy_ctx;
};

typedef struct {
    Mel_Gpu_Device* dev;
    u32 frame_count;
    const Mel_Alloc* alloc;
} Mel_Render_Graph_Opt;

void mel_render_graph_init_opt(Mel_Render_Graph* g, Mel_Render_Graph_Opt opt);
#define mel_render_graph_init(g, ...) mel_render_graph_init_opt((g), (Mel_Render_Graph_Opt){__VA_ARGS__})

void mel_render_graph_shutdown(Mel_Render_Graph* g);

u32 mel_render_graph_add_pass_opt(Mel_Render_Graph* g, str8 name, Mel_Pass_Desc desc);
#define mel_render_graph_add_pass(g, name, ...) mel_render_graph_add_pass_opt((g), (name), (Mel_Pass_Desc){__VA_ARGS__})

void mel_render_graph_remove_pass(Mel_Render_Graph* g, str8 name);

void mel_render_graph_pass_depends_on(Mel_Render_Graph* g, u32 pass_id, u32 dependency_id);

bool mel_render_graph_compile(Mel_Render_Graph* g);

bool mel_render_graph_execute(Mel_Render_Graph* g);
