#ifndef MEL_RENDER_PASS_H
#define MEL_RENDER_PASS_H

#include "render.graph.h"
#include "gpu.cmd.fwd.h"

typedef struct {
    f32 r;
    f32 g;
    f32 b;
    f32 a;
} Mel_Render_Pass_Clear_Data;

u32 mel_render_pass_clear(Mel_Render_Graph* graph, u32 resource_id,
                           f32 r, f32 g, f32 b, f32 a);

#endif
