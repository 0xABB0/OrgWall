#ifndef MEL_RENDER_GRAPH_H
#define MEL_RENDER_GRAPH_H

#include "types.h"
#include "allocator.h"
#include "string.str8.fwd.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "gpu.cmd.fwd.h"

#define MEL_RENDER_GRAPH_MAX_PASSES 64

typedef void (*Mel_Render_Graph_Execute_Fn)(Mel_Gpu_Cmd* cmd, void* user);

typedef struct Mel_Render_Graph_Pass Mel_Render_Graph_Pass;
typedef struct Mel_Render_Graph_Resource Mel_Render_Graph_Resource;
typedef struct Mel_Render_Graph Mel_Render_Graph;

struct Mel_Render_Graph_Pass {
    u32 id;
    str8 name;
    u64 depends_on;
    u64 write_mask;
    u64 read_mask;
    Mel_Render_Graph_Execute_Fn execute;
    void* user;
};

struct Mel_Render_Graph_Resource {
    u32 id;
    str8 name;
    VkFormat format;
    u32 width;
    u32 height;
    bool is_backbuffer;
};

struct Mel_Render_Graph {
    Mel_Render_Graph_Pass passes[MEL_RENDER_GRAPH_MAX_PASSES];
    u32 pass_count;

    Mel_Render_Graph_Resource resources[MEL_RENDER_GRAPH_MAX_PASSES];
    u32 resource_count;

    u32 sorted_order[MEL_RENDER_GRAPH_MAX_PASSES];
    u32 sorted_count;

    bool built;
    const Mel_Alloc* alloc;
};

typedef struct {
    const Mel_Alloc* alloc;
} Mel_Render_Graph_Opt;

void mel_render_graph_init_opt(Mel_Render_Graph* g, Mel_Render_Graph_Opt opt);
#define mel_render_graph_init(g, ...) mel_render_graph_init_opt((g), (Mel_Render_Graph_Opt){__VA_ARGS__})

void mel_render_graph_shutdown(Mel_Render_Graph* g);

u32 mel_render_graph_add_pass(Mel_Render_Graph* g, str8 name,
                               Mel_Render_Graph_Execute_Fn execute, void* user);

u32 mel_render_graph_add_resource(Mel_Render_Graph* g, str8 name,
                                   VkFormat format, u32 width, u32 height, bool is_backbuffer);

void mel_render_graph_pass_writes(Mel_Render_Graph* g, u32 pass_id, u32 resource_id);
void mel_render_graph_pass_reads(Mel_Render_Graph* g, u32 pass_id, u32 resource_id);
void mel_render_graph_pass_depends_on(Mel_Render_Graph* g, u32 pass_id, u32 dependency_id);

bool mel_render_graph_build(Mel_Render_Graph* g);

void mel_render_graph_execute(Mel_Render_Graph* g, Mel_Gpu_Cmd* cmd);

#endif
