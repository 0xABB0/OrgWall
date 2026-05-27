#include "metal.h"

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    if (!sc) return;
    sc->drawable   = [sc->layer nextDrawable];
    sc->cmd_buffer = sc->drawable ? [sc->device->queue commandBuffer] : nil;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc)
{
    return sc ? &sc->cmd : NULL;
}

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc || !sc->cmd_buffer) return;
    if (sc->drawable) [sc->cmd_buffer presentDrawable:sc->drawable];
    [sc->cmd_buffer commit];
    sc->cmd_buffer = nil;
    sc->drawable   = nil;
}

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    if (!cmd) return;
    Mel_Gpu_Swapchain* sc = cmd->swapchain;
    if (!sc->cmd_buffer || !sc->drawable) return;

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = sc->drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(clear.r, clear.g, clear.b, clear.a);

    cmd->encoder = [sc->cmd_buffer renderCommandEncoderWithDescriptor:rp];

    MTLViewport vp = { 0.0, 0.0, (double)sc->layer.drawableSize.width,
                       (double)sc->layer.drawableSize.height, 0.0, 1.0 };
    [cmd->encoder setViewport:vp];
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    if (!cmd || !cmd->encoder) return;
    [cmd->encoder endEncoding];
    cmd->encoder = nil;
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline* pipe)
{
    if (!cmd || !cmd->encoder || !pipe) return;
    [cmd->encoder setRenderPipelineState:pipe->state];
    [cmd->encoder setCullMode:pipe->cull];
    cmd->primitive = pipe->primitive;
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer* buf)
{
    if (!cmd || !cmd->encoder || !buf) return;
    [cmd->encoder setVertexBuffer:buf->mtl offset:0 atIndex:slot];
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    if (!cmd || !cmd->encoder) return;
    [cmd->encoder drawPrimitives:cmd->primitive vertexStart:0
                     vertexCount:vertex_count instanceCount:instance_count];
}
