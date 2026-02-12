#define VK_NO_PROTOTYPES
#include "render.pass.h"
#include "render.graph.h"
#include "gpu.cmd.h"
#include "string.str8.h"

static void clear_execute(Mel_Gpu_Cmd* cmd, void* user)
{
    MEL_UNUSED(cmd);
    MEL_UNUSED(user);
}

u32 mel_render_pass_clear(Mel_Render_Graph* graph, u32 resource_id,
                           f32 r, f32 g, f32 b, f32 a)
{
    assert(graph != nullptr);
    MEL_UNUSED(r);
    MEL_UNUSED(g);
    MEL_UNUSED(b);
    MEL_UNUSED(a);

    u32 pass = mel_render_graph_add_pass(graph, S8("clear"), clear_execute, nullptr);
    mel_render_graph_pass_writes(graph, pass, resource_id);
    return pass;
}
