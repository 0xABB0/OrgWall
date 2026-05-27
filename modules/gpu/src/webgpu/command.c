#include "webgpu_backend.h"

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    if (!sc) return;

    WGPUSurfaceTexture st = {0};
    wgpuSurfaceGetCurrentTexture(sc->surface, &st);
    if (!st.texture) { sc->cur_texture = NULL; sc->cur_view = NULL; sc->cmd.encoder = NULL; return; }

    sc->cur_texture   = st.texture;
    sc->cur_view      = wgpuTextureCreateView(st.texture, NULL);
    sc->cmd.encoder   = wgpuDeviceCreateCommandEncoder(sc->device->device, NULL);
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc)
{
    return sc ? &sc->cmd : NULL;
}

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc || !sc->cmd.encoder) return;

    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(sc->cmd.encoder, NULL);
    wgpuQueueSubmit(sc->device->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(sc->cmd.encoder);
    sc->cmd.encoder = NULL;

#if !defined(__EMSCRIPTEN__)
    // Native WebGPU (e.g. Dawn) presents explicitly. On Emscripten the browser
    // composites the canvas when control returns to the event loop, and
    // wgpuSurfacePresent is unsupported there.
    wgpuSurfacePresent(sc->surface);
#endif

    if (sc->cur_view)    wgpuTextureViewRelease(sc->cur_view);
    if (sc->cur_texture) wgpuTextureRelease(sc->cur_texture);
    sc->cur_view    = NULL;
    sc->cur_texture = NULL;
}

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    if (!cmd || !cmd->encoder || !cmd->swapchain->cur_view) return;

    WGPURenderPassColorAttachment att = {
        .view       = cmd->swapchain->cur_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp     = WGPULoadOp_Clear,
        .storeOp    = WGPUStoreOp_Store,
        .clearValue = { clear.r, clear.g, clear.b, clear.a },
    };
    WGPURenderPassDescriptor rp = {
        .colorAttachmentCount = 1,
        .colorAttachments     = &att,
    };
    cmd->pass = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &rp);
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    if (!cmd || !cmd->pass) return;
    wgpuRenderPassEncoderEnd(cmd->pass);
    wgpuRenderPassEncoderRelease(cmd->pass);
    cmd->pass = NULL;
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline* pipe)
{
    if (!cmd || !cmd->pass || !pipe) return;
    wgpuRenderPassEncoderSetPipeline(cmd->pass, pipe->pipe);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer* buf)
{
    if (!cmd || !cmd->pass || !buf) return;
    wgpuRenderPassEncoderSetVertexBuffer(cmd->pass, slot, buf->buf, 0, buf->size);
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    if (!cmd || !cmd->pass) return;
    wgpuRenderPassEncoderDraw(cmd->pass, vertex_count, instance_count, 0, 0);
}
