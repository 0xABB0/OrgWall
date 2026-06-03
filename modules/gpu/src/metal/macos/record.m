#include "mtl_backend.h"

#include <log/log.h>

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    Mel_Gpu_Device* dev = sc->dev;
    sc->frame_ok = false;

    if (sc->frame_serial)
        mel_gpu__submit_complete(dev, sc->frame_serial);

    id<CAMetalDrawable> drawable = [sc->surface->layer nextDrawable];
    if (!drawable)
    {
        mel_log_warn("gpu", "frame_begin: nextDrawable returned nil (window minimized or surface lost); skipping frame");
        return;
    }

    id<MTLCommandBuffer> cb = [dev->queue commandBuffer];
    if (!cb)
    {
        mel_log_error("gpu", "frame_begin: commandBuffer returned nil");
        return;
    }

    sc->drawable = drawable;
    sc->recorder.cb = cb;
    sc->recorder.encoder = nil;
    sc->recorder.has_pipeline = false;
    sc->recorder.index_buffer = nil;
    sc->frame_ok = true;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc) { return &sc->recorder; }

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc->frame_ok)
        return;
    Mel_Gpu_Device* dev = sc->dev;

    if (sc->recorder.encoder)
    {
        [sc->recorder.encoder endEncoding];
        sc->recorder.encoder = nil;
    }

    u64 serial = mel_gpu__submit_serial_next(dev);
    sc->frame_serial = serial;

    [sc->recorder.cb presentDrawable:sc->drawable];

    __block Mel_Gpu_Device* bdev = dev;
    __block u64             bserial = serial;
    [sc->recorder.cb addCompletedHandler:^(id<MTLCommandBuffer> buf) {
        (void)buf;
        mel_gpu__submit_complete(bdev, bserial);
    }];

    [sc->recorder.cb commit];

    sc->recorder.cb = nil;
    sc->drawable = nil;
    sc->frame_ok = false;
}

void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    Mel_Gpu_Swapchain* sc = cmd->sc;
    if (!sc || !sc->drawable || !cmd->cb)
    {
        mel_log_error("gpu", "cmd_begin_pass: no active swapchain frame");
        return;
    }

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = sc->drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(clear.r, clear.g, clear.b, clear.a);

    cmd->encoder = [cmd->cb renderCommandEncoderWithDescriptor:rp];
    cmd->has_pipeline = false;

    MTLViewport vp = { 0.0, 0.0, (double)sc->width, (double)sc->height, 0.0, 1.0 };
    [cmd->encoder setViewport:vp];
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    if (cmd->encoder)
    {
        [cmd->encoder endEncoding];
        cmd->encoder = nil;
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
    cmd->cb = [cmd->dev->queue commandBuffer];
    cmd->encoder = nil;
    cmd->warned_unsupported = false;
    cmd->recording = true;
    cmd->has_pipeline = false;
    cmd->index_buffer = nil;
}

void mel_gpu_command_list_end(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->recording);
    if (cmd->encoder)
    {
        [cmd->encoder endEncoding];
        cmd->encoder = nil;
    }
    cmd->recording = false;
    mel_gpu__track_exit(cmd->dev, cmd);
}

void mel_gpu_command_list_destroy(Mel_Gpu_Command_List* cmd)
{
    if (!cmd)
        return;
    mel_assert(cmd->standalone);
    cmd->cb = nil;
    cmd->encoder = nil;
    mel_dealloc(cmd->dev->alloc, cmd);
}

void mel_gpu_cmd_texture_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range range, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    (void)tex;
    (void)range;
    (void)src;
    (void)dst;
    (void)cmd;
}

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    (void)buf;
    (void)src;
    (void)dst;
    (void)cmd;
}

void mel_gpu_cmd_copy_texture_to_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range subresource, Mel_Gpu_Buffer dst)
{
    if (!cmd || !cmd->cb)
    {
        mel_log_error("gpu", "cmd_copy_texture_to_buffer: command list has no command buffer");
        return;
    }

    Mel_Gpu_Texture_Obj to;
    id<MTLBuffer>       db = nil;
    if (!mel_gpu__texture_get(cmd->dev, tex, &to) || !mel_gpu__buffer_get(cmd->dev, dst, &db))
    {
        mel_log_error("gpu", "cmd_copy_texture_to_buffer: source texture or destination buffer is not a live handle");
        return;
    }

    if (cmd->encoder)
    {
        [cmd->encoder endEncoding];
        cmd->encoder = nil;
    }

    id<MTLTexture> src = (__bridge id<MTLTexture>)to.texture;
    u32            mip = subresource.base_mip;
    u32            layer = subresource.base_layer;
    u32            w = to.width >> mip ? to.width >> mip : 1;
    u32            h = to.height >> mip ? to.height >> mip : 1;
    usize          row_bytes = (usize)w * 4;

    id<MTLBlitCommandEncoder> blit = [cmd->cb blitCommandEncoder];
    [blit copyFromTexture:src sourceSlice:layer sourceLevel:mip sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1) toBuffer:db destinationOffset:0 destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * h];
    [blit endEncoding];
}

void mel_gpu_cmd_copy_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer src, Mel_Gpu_Buffer dst, usize bytes)
{
    if (!cmd || !cmd->cb)
    {
        mel_log_error("gpu", "cmd_copy_buffer: command list has no command buffer");
        return;
    }
    id<MTLBuffer> sb = nil, db = nil;
    if (!mel_gpu__buffer_get(cmd->dev, src, &sb) || !mel_gpu__buffer_get(cmd->dev, dst, &db))
    {
        mel_log_error("gpu", "cmd_copy_buffer: source or destination buffer is not a live handle");
        return;
    }
    if (cmd->encoder)
    {
        [cmd->encoder endEncoding];
        cmd->encoder = nil;
    }
    id<MTLBlitCommandEncoder> blit = [cmd->cb blitCommandEncoder];
    [blit copyFromBuffer:sb sourceOffset:0 toBuffer:db destinationOffset:0 size:bytes];
    [blit endEncoding];
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj o;
    if (!mel_gpu__pipeline_get(cmd->dev, pipe, &o) || !o.state)
    {
        mel_log_error("gpu", "cmd_bind_pipeline: pipeline is not a live handle");
        return;
    }
    if (o.compute)
    {
        mel_log_error("gpu", "cmd_bind_pipeline: compute pipelines are not bindable on the Metal backend's render-encoder command list this round (MissingFeature)");
        return;
    }
    if (!cmd->encoder)
    {
        mel_log_error("gpu", "cmd_bind_pipeline: no active render encoder (call begin_pass / begin_rendering first)");
        return;
    }
    [cmd->encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)o.state];
    cmd->primitive = mel_gpu__topology_to_primitive(o.topology);
    cmd->has_pipeline = true;
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf)
{
    (void)slot;
    id<MTLBuffer> mb = nil;
    if (!mel_gpu__buffer_get(cmd->dev, buf, &mb))
    {
        mel_log_error("gpu", "cmd_bind_vertex_buffer: buffer is not a live handle");
        return;
    }
    if (!cmd->encoder)
    {
        mel_log_error("gpu", "cmd_bind_vertex_buffer: no active render encoder");
        return;
    }
    [cmd->encoder setVertexBuffer:mb offset:0 atIndex:MEL_GPU_METAL_VERTEX_BUFFER_INDEX];
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Index_Type type)
{
    id<MTLBuffer> mb = nil;
    if (!mel_gpu__buffer_get(cmd->dev, buf, &mb))
    {
        mel_log_error("gpu", "cmd_bind_index_buffer: buffer is not a live handle");
        return;
    }
    cmd->index_buffer = mb;
    cmd->index_type = type == MEL_GPU_INDEX_UINT32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 bytes, const void* data)
{
    if (!cmd->encoder)
    {
        mel_log_error("gpu", "cmd_push_constants: no active render encoder");
        return;
    }
    if (offset != 0)
    {
        mel_log_error("gpu", "cmd_push_constants: nonzero offset %u unsupported on the Metal backend (push constants ride a single buffer slot)", offset);
        return;
    }
    [cmd->encoder setVertexBytes:data length:bytes atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
    [cmd->encoder setFragmentBytes:data length:bytes atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    if (!cmd->encoder || !cmd->has_pipeline)
    {
        mel_log_error("gpu", "cmd_draw: no active render encoder with a bound pipeline");
        return;
    }
    [cmd->encoder drawPrimitives:cmd->primitive vertexStart:0 vertexCount:vertex_count instanceCount:instance_count ? instance_count : 1];
}

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count)
{
    if (!cmd->encoder || !cmd->has_pipeline)
    {
        mel_log_error("gpu", "cmd_draw_indexed: no active render encoder with a bound pipeline");
        return;
    }
    if (!cmd->index_buffer)
    {
        mel_log_error("gpu", "cmd_draw_indexed: no index buffer bound");
        return;
    }
    [cmd->encoder drawIndexedPrimitives:cmd->primitive indexCount:index_count indexType:cmd->index_type indexBuffer:cmd->index_buffer indexBufferOffset:0 instanceCount:instance_count ? instance_count : 1];
}

void mel_gpu_cmd_dispatch(Mel_Gpu_Command_List* cmd, u32 groups_x, u32 groups_y, u32 groups_z)
{
    (void)groups_x;
    (void)groups_y;
    (void)groups_z;
    mel_log_error("gpu", "cmd_dispatch: compute dispatch is unsupported on the Metal backend's render-encoder command list this round (MissingFeature)");
}

void mel_gpu_cmd_dispatch_indirect(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer args, usize offset)
{
    (void)cmd;
    (void)args;
    (void)offset;
    mel_log_error("gpu", "cmd_dispatch_indirect: compute dispatch is unsupported on the Metal backend this round (MissingFeature)");
}
