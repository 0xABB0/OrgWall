#include "wgpu_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

static void mel_gpu__warn_unsupported(Mel_Gpu_Command_List* cmd, const char* what)
{
    if (cmd && !cmd->warned_unsupported)
    {
        cmd->warned_unsupported = true;
        mel_log_warn("gpu", "webgpu backend: %s is not implemented this round; the call is a loud no-op (MissingFeature)", what);
    }
    else if (!cmd)
    {
        mel_log_warn("gpu", "webgpu backend: %s is not implemented this round (MissingFeature)", what);
    }
}

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    Mel_Gpu_Device* dev = sc->dev;
    sc->frame_ok = false;

    if (sc->frame_view)
    {
        wgpuTextureViewRelease(sc->frame_view);
        sc->frame_view = NULL;
    }
    if (sc->frame_texture)
    {
        wgpuTextureRelease(sc->frame_texture);
        sc->frame_texture = NULL;
    }

    WGPUSurfaceTexture st = { 0 };
    wgpuSurfaceGetCurrentTexture(sc->surface->wgpu, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        mel_log_warn("gpu", "frame_begin: getCurrentTexture status %d (surface lost or out of date); skipping frame", (int)st.status);
        if (st.texture)
            wgpuTextureRelease(st.texture);
        return;
    }

    WGPUTextureView view = wgpuTextureCreateView(st.texture, NULL);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(dev->wgpu, NULL);
    if (!view || !enc)
    {
        mel_log_error("gpu", "frame_begin: failed to create swapchain view or command encoder");
        if (view)
            wgpuTextureViewRelease(view);
        if (enc)
            wgpuCommandEncoderRelease(enc);
        wgpuTextureRelease(st.texture);
        return;
    }

    sc->frame_texture = st.texture;
    sc->frame_view = view;
    sc->recorder.encoder = enc;
    sc->recorder.pass = NULL;
    sc->recorder.has_bound = false;
    sc->frame_ok = true;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc) { return &sc->recorder; }

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc->frame_ok)
        return;
    Mel_Gpu_Device* dev = sc->dev;

    if (sc->recorder.pass)
    {
        wgpuRenderPassEncoderEnd(sc->recorder.pass);
        wgpuRenderPassEncoderRelease(sc->recorder.pass);
        sc->recorder.pass = NULL;
    }

    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(sc->recorder.encoder, NULL);
    wgpuCommandEncoderRelease(sc->recorder.encoder);
    sc->recorder.encoder = NULL;

    u64 serial = mel_gpu__submit_serial_next(dev);
    wgpuQueueSubmit(dev->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);

    mel_gpu__submit_complete(dev, serial);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(sc->surface->wgpu);
#endif
    wgpuInstanceProcessEvents(dev->wgpu_instance);

    wgpuTextureViewRelease(sc->frame_view);
    wgpuTextureRelease(sc->frame_texture);
    sc->frame_view = NULL;
    sc->frame_texture = NULL;
    sc->frame_ok = false;
}

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    Mel_Gpu_Swapchain* sc = cmd->sc;
    if (!sc || !sc->frame_view || !cmd->encoder)
    {
        mel_log_error("gpu", "cmd_begin_pass: no active swapchain frame");
        return;
    }

    WGPURenderPassColorAttachment color = {
        .view = sc->frame_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { clear.r, clear.g, clear.b, clear.a },
    };
    WGPURenderPassDescriptor rp = { .colorAttachmentCount = 1, .colorAttachments = &color };
    cmd->pass = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &rp);
    wgpuRenderPassEncoderSetViewport(cmd->pass, 0.0f, 0.0f, (float)sc->width, (float)sc->height, 0.0f, 1.0f);
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    if (cmd->pass)
    {
        wgpuRenderPassEncoderEnd(cmd->pass);
        wgpuRenderPassEncoderRelease(cmd->pass);
        cmd->pass = NULL;
    }
}

Mel_Gpu_Command_List* mel_gpu_command_list_create(Mel_Gpu_Queue* q)
{
    if (!q)
        return NULL;
    Mel_Gpu_Device*       dev = q->dev;
    Mel_Gpu_Command_List* cmd = mel_alloc_type(dev->alloc, Mel_Gpu_Command_List);
    *cmd = (Mel_Gpu_Command_List){ .dev = dev, .standalone = true };
    return cmd;
}

void mel_gpu_command_list_begin(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->standalone);
    mel_gpu__track_enter(cmd->dev, cmd, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    cmd->encoder = wgpuDeviceCreateCommandEncoder(cmd->dev->wgpu, NULL);
    cmd->pass = NULL;
    cmd->has_bound = false;
    cmd->warned_unsupported = false;
    cmd->recording = true;
}

void mel_gpu_command_list_end(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->recording);
    if (cmd->pass)
    {
        wgpuRenderPassEncoderEnd(cmd->pass);
        wgpuRenderPassEncoderRelease(cmd->pass);
        cmd->pass = NULL;
    }
    cmd->recording = false;
    mel_gpu__track_exit(cmd->dev, cmd);
}

void mel_gpu_command_list_destroy(Mel_Gpu_Command_List* cmd)
{
    if (!cmd)
        return;
    mel_assert(cmd->standalone);
    if (cmd->pass)
        wgpuRenderPassEncoderRelease(cmd->pass);
    if (cmd->encoder)
        wgpuCommandEncoderRelease(cmd->encoder);
    mel_dealloc(cmd->dev->alloc, cmd);
}

void mel_gpu_cmd_texture_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range range, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    (void)cmd;
    (void)tex;
    (void)range;
    (void)src;
    (void)dst;
    /* WebGPU inserts barriers automatically at pass boundaries (spec §3.1 P1: barriers no-op). */
}

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    (void)cmd;
    (void)buf;
    (void)src;
    (void)dst;
}

void mel_gpu_cmd_copy_texture_to_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range subresource, Mel_Gpu_Buffer dst)
{
    Mel_Gpu_Texture_Obj t;
    Mel_Gpu_Buffer_Obj  b;
    if (!cmd->encoder || !mel_gpu__texture_get(cmd->dev, tex, &t) || !mel_gpu__buffer_get(cmd->dev, dst, &b))
    {
        mel_log_error("gpu", "cmd_copy_texture_to_buffer: invalid encoder/texture/buffer");
        return;
    }
    u32 bpp = mel_gpu_format_bytes(mel_gpu__wgpu_format_to_mel(t.format));

    WGPUTexelCopyTextureInfo src = {
        .texture = t.wgpu,
        .mipLevel = subresource.base_mip,
        .origin = { 0, 0, subresource.base_layer },
        .aspect = WGPUTextureAspect_All,
    };
    WGPUTexelCopyBufferInfo bufinfo = {
        .layout = { .offset = 0, .bytesPerRow = t.width * bpp, .rowsPerImage = t.height },
        .buffer = b.wgpu,
    };
    WGPUExtent3D size = { t.width, t.height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(cmd->encoder, &src, &bufinfo, &size);
}

void mel_gpu_cmd_copy_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer src, Mel_Gpu_Buffer dst, usize bytes)
{
    Mel_Gpu_Buffer_Obj s, d;
    if (!cmd->encoder || !mel_gpu__buffer_get(cmd->dev, src, &s) || !mel_gpu__buffer_get(cmd->dev, dst, &d))
    {
        mel_log_error("gpu", "cmd_copy_buffer: invalid encoder or buffer handle");
        return;
    }
    wgpuCommandEncoderCopyBufferToBuffer(cmd->encoder, s.wgpu, 0, d.wgpu, 0, bytes);
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj o;
    if (!mel_gpu__table_get_copy(cmd->dev, &cmd->dev->pipelines, pipe.slot, &o) || !o.render)
    {
        mel_log_error("gpu", "cmd_bind_pipeline: pipeline handle has no render pipeline");
        return;
    }
    if (!cmd->pass)
    {
        mel_log_error("gpu", "cmd_bind_pipeline: no active render pass");
        return;
    }
    wgpuRenderPassEncoderSetPipeline(cmd->pass, o.render);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj o;
    if (!cmd->pass || !mel_gpu__buffer_get(cmd->dev, buf, &o))
    {
        mel_log_error("gpu", "cmd_bind_vertex_buffer: no active pass or invalid buffer");
        return;
    }
    wgpuRenderPassEncoderSetVertexBuffer(cmd->pass, slot, o.wgpu, 0, o.size);
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Index_Type type)
{
    Mel_Gpu_Buffer_Obj o;
    if (!cmd->pass || !mel_gpu__buffer_get(cmd->dev, buf, &o))
    {
        mel_log_error("gpu", "cmd_bind_index_buffer: no active pass or invalid buffer");
        return;
    }
    WGPUIndexFormat fmt = type == MEL_GPU_INDEX_UINT16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
    wgpuRenderPassEncoderSetIndexBuffer(cmd->pass, o.wgpu, fmt, 0, o.size);
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 size, const void* data)
{
    (void)offset;
    (void)size;
    (void)data;
    mel_gpu__warn_unsupported(cmd, "cmd_push_constants (push constants are not in WebGPU core)");
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    if (!cmd->pass)
    {
        mel_log_error("gpu", "cmd_draw: no active render pass");
        return;
    }
    wgpuRenderPassEncoderDraw(cmd->pass, vertex_count, instance_count ? instance_count : 1, 0, 0);
}

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count)
{
    if (!cmd->pass)
    {
        mel_log_error("gpu", "cmd_draw_indexed: no active render pass");
        return;
    }
    wgpuRenderPassEncoderDrawIndexed(cmd->pass, index_count, instance_count ? instance_count : 1, 0, 0, 0);
}

void mel_gpu_cmd_dispatch(Mel_Gpu_Command_List* cmd, u32 groups_x, u32 groups_y, u32 groups_z)
{
    (void)groups_x;
    (void)groups_y;
    (void)groups_z;
    mel_gpu__warn_unsupported(cmd, "cmd_dispatch (compute pipelines not implemented this round)");
}

void mel_gpu_cmd_dispatch_indirect(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer args, usize offset)
{
    (void)args;
    (void)offset;
    mel_gpu__warn_unsupported(cmd, "cmd_dispatch_indirect");
}
