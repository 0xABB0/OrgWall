#define VK_NO_PROTOTYPES
#include "render.graph.h"
#include "string.str8.h"
#include "gpu.cmd.h"
#include <string.h>

void mel_render_graph_init_opt(Mel_Render_Graph* g, Mel_Render_Graph_Opt opt)
{
    assert(g != nullptr);

    *g = (Mel_Render_Graph){0};
    g->alloc = opt.alloc;
}

void mel_render_graph_shutdown(Mel_Render_Graph* g)
{
    assert(g != nullptr);
    *g = (Mel_Render_Graph){0};
}

u32 mel_render_graph_add_pass(Mel_Render_Graph* g, str8 name,
                               Mel_Render_Graph_Execute_Fn execute, void* user)
{
    assert(g != nullptr);
    assert(g->pass_count < MEL_RENDER_GRAPH_MAX_PASSES);
    assert(!g->built);

    u32 id = g->pass_count;
    g->passes[id] = (Mel_Render_Graph_Pass){
        .id = id,
        .name = name,
        .execute = execute,
        .user = user,
    };
    g->pass_count++;
    return id;
}

u32 mel_render_graph_add_resource(Mel_Render_Graph* g, str8 name,
                                   VkFormat format, u32 width, u32 height, bool is_backbuffer)
{
    assert(g != nullptr);
    assert(g->resource_count < MEL_RENDER_GRAPH_MAX_PASSES);
    assert(!g->built);

    u32 id = g->resource_count;
    g->resources[id] = (Mel_Render_Graph_Resource){
        .id = id,
        .name = name,
        .format = format,
        .width = width,
        .height = height,
        .is_backbuffer = is_backbuffer,
    };
    g->resource_count++;
    return id;
}

void mel_render_graph_pass_writes(Mel_Render_Graph* g, u32 pass_id, u32 resource_id)
{
    assert(g != nullptr);
    assert(pass_id < g->pass_count);
    assert(resource_id < 64);
    assert(!g->built);
    g->passes[pass_id].write_mask |= (1ULL << resource_id);
}

void mel_render_graph_pass_reads(Mel_Render_Graph* g, u32 pass_id, u32 resource_id)
{
    assert(g != nullptr);
    assert(pass_id < g->pass_count);
    assert(resource_id < 64);
    assert(!g->built);
    g->passes[pass_id].read_mask |= (1ULL << resource_id);
}

void mel_render_graph_pass_depends_on(Mel_Render_Graph* g, u32 pass_id, u32 dependency_id)
{
    assert(g != nullptr);
    assert(pass_id < g->pass_count);
    assert(dependency_id < g->pass_count);
    assert(pass_id != dependency_id);
    assert(!g->built);
    g->passes[pass_id].depends_on |= (1ULL << dependency_id);
}

static void compute_auto_dependencies(Mel_Render_Graph* g)
{
    for (u32 i = 0; i < g->pass_count; i++)
    {
        Mel_Render_Graph_Pass* pass = &g->passes[i];

        if (pass->read_mask == 0)
            continue;

        for (u32 j = 0; j < g->pass_count; j++)
        {
            if (i == j) continue;

            Mel_Render_Graph_Pass* other = &g->passes[j];

            if (other->write_mask & pass->read_mask)
                pass->depends_on |= (1ULL << j);
        }
    }

    for (u32 i = 0; i < g->pass_count; i++)
    {
        Mel_Render_Graph_Pass* pass = &g->passes[i];

        if (pass->write_mask == 0)
            continue;

        for (u32 j = 0; j < g->pass_count; j++)
        {
            if (i == j) continue;

            Mel_Render_Graph_Pass* other = &g->passes[j];

            if ((other->write_mask & pass->write_mask) && j < i)
                pass->depends_on |= (1ULL << j);
        }
    }
}

bool mel_render_graph_build(Mel_Render_Graph* g)
{
    assert(g != nullptr);
    assert(!g->built);

    compute_auto_dependencies(g);

    u64 scheduled = 0;
    g->sorted_count = 0;

    for (u32 iter = 0; iter < g->pass_count; iter++)
    {
        bool progress = false;

        for (u32 i = 0; i < g->pass_count; i++)
        {
            if (scheduled & (1ULL << i))
                continue;

            if ((g->passes[i].depends_on & ~scheduled) == 0)
            {
                g->sorted_order[g->sorted_count++] = i;
                scheduled |= (1ULL << i);
                progress = true;
                break;
            }
        }

        if (!progress)
            return false;
    }

    g->built = true;
    return true;
}

void mel_render_graph_execute(Mel_Render_Graph* g, Mel_Gpu_Cmd* cmd)
{
    assert(g != nullptr);
    assert(g->built);

    for (u32 i = 0; i < g->sorted_count; i++)
    {
        Mel_Render_Graph_Pass* pass = &g->passes[g->sorted_order[i]];
        if (pass->execute)
            pass->execute(cmd, pass->user);
    }
}
