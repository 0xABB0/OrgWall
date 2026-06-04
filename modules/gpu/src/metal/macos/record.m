#include "mtl_backend.h"

#include <log/log.h>

#include <string.h>

void mel_gpu__cmd_end_active_encoder(Mel_Gpu_Command_List* cmd)
{
    if (cmd->encoder)
    {
        [cmd->encoder endEncoding];
        cmd->encoder = nil;
    }
    if (cmd->compute_encoder)
    {
        [cmd->compute_encoder endEncoding];
        cmd->compute_encoder = nil;
        cmd->has_compute_pipeline = false;
    }
}

static void mel_gpu__cmd_open_compute_encoder(Mel_Gpu_Command_List* cmd)
{
    if (cmd->compute_encoder)
        return;
    mel_gpu__cmd_end_active_encoder(cmd);
    cmd->compute_encoder = [cmd->cb computeCommandEncoder];
    cmd->has_compute_pipeline = false;
}

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
    sc->recorder.compute_encoder = nil;
    sc->recorder.has_pipeline = false;
    sc->recorder.has_compute_pipeline = false;
    sc->recorder.index_buffer = nil;
    sc->frame_ok = true;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc) { return &sc->recorder; }

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc->frame_ok)
        return;
    Mel_Gpu_Device* dev = sc->dev;

    mel_gpu__cmd_end_active_encoder(&sc->recorder);

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

    mel_gpu__cmd_end_active_encoder(cmd);

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
    Mel_Gpu_Command_List* cmd = mel_calloc(dev->alloc, sizeof(Mel_Gpu_Command_List));
    cmd->dev = dev;
    cmd->standalone = true;
    return cmd;
}

void mel_gpu_command_list_begin(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->standalone);
    mel_gpu__track_enter(cmd->dev, cmd, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    cmd->cb = [cmd->dev->queue commandBuffer];
    cmd->encoder = nil;
    cmd->compute_encoder = nil;
    cmd->warned_unsupported = false;
    cmd->recording = true;
    cmd->has_pipeline = false;
    cmd->has_compute_pipeline = false;
    cmd->index_buffer = nil;
    cmd->pc_stashed = false;
    cmd->pc_stash_len = 0;
}

void mel_gpu_command_list_end(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->recording);
    mel_gpu__cmd_end_active_encoder(cmd);
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
    cmd->compute_encoder = nil;
    if (cmd->pc_stash)
        mel_dealloc(cmd->dev->alloc, cmd->pc_stash);
    mel_dealloc(cmd->dev->alloc, cmd);
}

void mel_gpu_cmd_texture_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range range, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    (void)tex;
    (void)range;
    (void)src;
    (void)dst;
    if (cmd && cmd->compute_encoder)
        [cmd->compute_encoder memoryBarrierWithScope:MTLBarrierScopeTextures | MTLBarrierScopeBuffers];
}

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    (void)buf;
    (void)src;
    (void)dst;
    if (cmd && cmd->compute_encoder)
        [cmd->compute_encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
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

    mel_gpu__cmd_end_active_encoder(cmd);

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
    mel_gpu__cmd_end_active_encoder(cmd);
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
        if (!o.state)
        {
            mel_log_error("gpu", "cmd_bind_pipeline: compute pipeline has no MTLComputePipelineState");
            return;
        }
        mel_gpu__cmd_open_compute_encoder(cmd);
        cmd->compute_state = (__bridge id<MTLComputePipelineState>)o.state;
        cmd->compute_threadgroup = o.threadgroup;
        cmd->compute_pipeline_handle = pipe;
        cmd->pc_stashed = false;
        cmd->pc_stash_len = 0;
        [cmd->compute_encoder setComputePipelineState:cmd->compute_state];
        cmd->has_compute_pipeline = true;
        return;
    }
    if (!cmd->encoder)
    {
        mel_log_error("gpu", "cmd_bind_pipeline: no active render encoder (call begin_pass / begin_rendering first)");
        return;
    }
    [cmd->encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)o.state];
    [cmd->encoder setCullMode:o.cull_mode];
    [cmd->encoder setFrontFacingWinding:o.front_face];
    [cmd->encoder setTriangleFillMode:o.fill_mode];
    if (o.depth_stencil_state)
        [cmd->encoder setDepthStencilState:(__bridge id<MTLDepthStencilState>)o.depth_stencil_state];
    if (o.stencil_test)
        [cmd->encoder setStencilFrontReferenceValue:o.stencil_ref_front backReferenceValue:o.stencil_ref_back];
    cmd->primitive = mel_gpu__topology_to_primitive(o.topology);
    cmd->gfx_pipeline_handle = pipe;
    cmd->pc_stashed = false;
    cmd->pc_stash_len = 0;
    cmd->has_pipeline = true;
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf)
{
    if (cmd->compute_encoder)
    {
        if (slot == MEL_GPU_METAL_PUSH_CONSTANT_INDEX)
        {
            mel_log_error("gpu",
                          "cmd_bind_vertex_buffer: on the compute encoder slot %u is reserved for push-constants (setBytes:); bind storage buffers at slot>=1 so the MSL kernel reads them at [[buffer(slot)]]",
                          MEL_GPU_METAL_PUSH_CONSTANT_INDEX);
            mel_assert(slot != MEL_GPU_METAL_PUSH_CONSTANT_INDEX);
            return;
        }
        id<MTLBuffer> cb = nil;
        if (!mel_gpu__buffer_get(cmd->dev, buf, &cb))
        {
            mel_log_error("gpu", "cmd_bind_vertex_buffer: buffer is not a live handle");
            return;
        }
        [cmd->compute_encoder setBuffer:cb offset:0 atIndex:slot];
        return;
    }
    if (slot >= MEL_GPU_METAL_VERTEX_BUFFER_BASE)
    {
        mel_log_error("gpu",
                      "cmd_bind_vertex_buffer: slot %u out of range; the Metal backend maps vertex slots onto buffer indices descending from %u (slot s -> index %u-s) to clear the push-constant index %u, so the usable slot range is 0..%u",
                      slot,
                      MEL_GPU_METAL_VERTEX_BUFFER_BASE,
                      MEL_GPU_METAL_VERTEX_BUFFER_BASE,
                      MEL_GPU_METAL_PUSH_CONSTANT_INDEX,
                      MEL_GPU_METAL_VERTEX_BUFFER_BASE - 1u);
        mel_assert(slot < MEL_GPU_METAL_VERTEX_BUFFER_BASE);
        return;
    }
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
    [cmd->encoder setVertexBuffer:mb offset:0 atIndex:MEL_GPU_METAL_VERTEX_SLOT_TO_INDEX(slot)];
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

static bool mel_gpu__compute_argbuffer_plan(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline_Obj* out)
{
    if (!cmd->has_compute_pipeline)
        return false;
    if (!mel_gpu__pipeline_get(cmd->dev, cmd->compute_pipeline_handle, out))
        return false;
    return out->arg_field_count != 0 && out->arg_encoder != NULL;
}

static bool mel_gpu__graphics_argbuffer_plan(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline_Obj* out)
{
    if (!cmd->has_pipeline)
        return false;
    if (!mel_gpu__pipeline_get(cmd->dev, cmd->gfx_pipeline_handle, out))
        return false;
    return out->arg_field_count != 0 && (out->vs_arg_encoder != NULL || out->fs_arg_encoder != NULL);
}

static void mel_gpu__pc_stash(Mel_Gpu_Command_List* cmd, const void* data, u32 bytes)
{
    if (bytes > cmd->pc_stash_cap)
    {
        if (cmd->pc_stash)
            mel_dealloc(cmd->dev->alloc, cmd->pc_stash);
        cmd->pc_stash = mel_alloc(cmd->dev->alloc, bytes);
        cmd->pc_stash_cap = bytes;
    }
    memcpy(cmd->pc_stash, data, bytes);
    cmd->pc_stash_len = bytes;
    cmd->pc_stashed = true;
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 bytes, const void* data)
{
    if (offset != 0)
    {
        mel_log_error("gpu", "cmd_push_constants: nonzero offset %u unsupported on the Metal backend (push constants ride a single buffer slot)", offset);
        return;
    }
    if (cmd->compute_encoder)
    {
        Mel_Gpu_Pipeline_Obj plan;
        if (mel_gpu__compute_argbuffer_plan(cmd, &plan))
        {
            if (bytes < plan.arg_host_size)
            {
                mel_log_error("gpu",
                              "cmd_push_constants: from-slang bindless pipeline expects %u host bytes for its inlined argument buffer but only %u supplied; the host push-constant struct does not match the shader Root",
                              plan.arg_host_size,
                              bytes);
                return;
            }
            mel_gpu__pc_stash(cmd, data, bytes);
            return;
        }
        [cmd->compute_encoder setBytes:data length:bytes atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
        return;
    }
    if (!cmd->encoder)
    {
        mel_log_error("gpu", "cmd_push_constants: no active render or compute encoder");
        return;
    }
    Mel_Gpu_Pipeline_Obj gfx_plan;
    if (mel_gpu__graphics_argbuffer_plan(cmd, &gfx_plan))
    {
        if (bytes < gfx_plan.arg_host_size)
        {
            mel_log_error("gpu",
                          "cmd_push_constants: from-slang bindless graphics pipeline expects %u host bytes for its inlined argument buffer but only %u supplied; the host push-constant struct does not match the shader Root",
                          gfx_plan.arg_host_size,
                          bytes);
            return;
        }
        mel_gpu__pc_stash(cmd, data, bytes);
        return;
    }
    [cmd->encoder setVertexBytes:data length:bytes atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
    [cmd->encoder setFragmentBytes:data length:bytes atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
}

static bool mel_gpu__build_graphics_argbuffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline_Obj* plan);

static bool mel_gpu__draw_prologue(Mel_Gpu_Command_List* cmd)
{
    Mel_Gpu_Pipeline_Obj plan;
    if (!mel_gpu__graphics_argbuffer_plan(cmd, &plan))
        return true;
    return mel_gpu__build_graphics_argbuffer(cmd, &plan);
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    if (!cmd->encoder || !cmd->has_pipeline)
    {
        mel_log_error("gpu", "cmd_draw: no active render encoder with a bound pipeline");
        return;
    }
    if (!mel_gpu__draw_prologue(cmd))
        return;
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
    if (!mel_gpu__draw_prologue(cmd))
        return;
    [cmd->encoder drawIndexedPrimitives:cmd->primitive indexCount:index_count indexType:cmd->index_type indexBuffer:cmd->index_buffer indexBufferOffset:0 instanceCount:instance_count ? instance_count : 1];
}

/* Build one per-dispatch/per-draw argument buffer from a from-slang bindless plan: allocate a
   transient MTLBuffer of the encoder's encodedLength, then for each plan field either resolve
   its 4-byte slot to a live bindless resource (texture/buffer/sampler) and inline it into the
   argument buffer, or memcpy the inline uniform bytes at the reflected member. Each resolved
   resource is made resident on whichever command encoder is live (compute or render). Returns
   the filled buffer (nil + a loud error on failure). The compute encoder reuses this through
   the render encoder's symmetric path — same Slang argument-buffer shape, same resolution. */
static id<MTLBuffer> mel_gpu__mtl_encode_arg_buffer(Mel_Gpu_Command_List* cmd, id<MTLArgumentEncoder> enc, usize encoded_length, const Mel_Gpu_Pipeline_Obj* plan, MTLRenderStages render_stages)
{
    Mel_Gpu_Device* dev = cmd->dev;
    id<MTLBuffer>   ab = [dev->mtl newBufferWithLength:encoded_length options:MTLResourceStorageModeShared];
    if (!ab)
    {
        mel_log_error("gpu", "from-slang bindless: failed to allocate %zu-byte argument buffer", encoded_length);
        return nil;
    }
    [enc setArgumentBuffer:ab offset:0];

    const u8* host = (const u8*)cmd->pc_stash;
    for (u32 i = 0; i < plan->arg_field_count; i++)
    {
        const Mel_Gpu_Mtl_Arg_Field* f = &plan->arg_fields[i];
        if (f->is_uniform)
        {
            void* dst = [enc constantDataAtIndex:f->arg_index];
            if (!dst)
            {
                mel_log_error("gpu", "from-slang bindless: argument encoder exposed no constant data at index %u for uniform field (host offset %u)", f->arg_index, f->host_offset);
                return nil;
            }
            memcpy(dst, host + f->host_offset, f->size);
            continue;
        }

        u32 slot = *(const u32*)(host + f->host_offset);
        u32 cls = mel_gpu__bindless_class_of_slang_kind(f->resource_kind);
        if (cls >= MEL_GPU_BINDLESS_BINDING_COUNT)
        {
            mel_log_error("gpu", "from-slang bindless: resource field (host offset %u) has unmappable resource kind %u", f->host_offset, f->resource_kind);
            return nil;
        }
        if (cls == MEL_GPU_BINDLESS_BINDING_SAMPLER)
        {
            id<MTLSamplerState> smp = mel_gpu__bindless_sampler(dev, slot);
            if (!smp)
            {
                mel_log_error("gpu", "from-slang bindless: sampler slot %u is not registered (no live sampler); cannot build the argument buffer", slot);
                return nil;
            }
            [enc setSamplerState:smp atIndex:f->arg_index];
            continue;
        }
        id<MTLResource> res = mel_gpu__bindless_resource(dev, cls, slot);
        if (!res)
        {
            mel_log_error("gpu", "from-slang bindless: slot %u (class %u) is not registered (no live resource); cannot build the argument buffer", slot, cls);
            return nil;
        }
        if (cls == MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE || cls == MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE)
            [enc setTexture:(id<MTLTexture>)res atIndex:f->arg_index];
        else
            [enc setBuffer:(id<MTLBuffer>)res offset:0 atIndex:f->arg_index];
        MTLResourceUsage usage = cls == MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE || cls == MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER ? (MTLResourceUsageRead | MTLResourceUsageWrite) : MTLResourceUsageRead;
        if (cmd->compute_encoder)
            [cmd->compute_encoder useResource:res usage:usage];
        else if (cmd->encoder)
            [cmd->encoder useResource:res usage:usage stages:render_stages];
    }
    return ab;
}

static bool mel_gpu__build_compute_argbuffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline_Obj* plan)
{
    if (!cmd->pc_stashed)
    {
        mel_log_error("gpu", "cmd_dispatch: from-slang bindless pipeline dispatched without cmd_push_constants; the inlined argument buffer carries the slot indices and inline uniforms");
        return false;
    }
    id<MTLArgumentEncoder> enc = (__bridge id<MTLArgumentEncoder>)plan->arg_encoder;
    id<MTLBuffer>          ab = mel_gpu__mtl_encode_arg_buffer(cmd, enc, plan->arg_encoded_length, plan, 0);
    if (!ab)
        return false;
    [cmd->compute_encoder setBuffer:ab offset:0 atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
    return true;
}

static bool mel_gpu__build_graphics_argbuffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline_Obj* plan)
{
    if (!cmd->pc_stashed)
    {
        mel_log_error("gpu", "cmd_draw: from-slang bindless graphics pipeline drawn without cmd_push_constants; the inlined argument buffer carries the slot indices and inline uniforms");
        return false;
    }
    if (plan->vs_arg_encoder)
    {
        id<MTLArgumentEncoder> enc = (__bridge id<MTLArgumentEncoder>)plan->vs_arg_encoder;
        id<MTLBuffer>          ab = mel_gpu__mtl_encode_arg_buffer(cmd, enc, plan->vs_arg_encoded_length, plan, MTLRenderStageVertex);
        if (!ab)
            return false;
        [cmd->encoder setVertexBuffer:ab offset:0 atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
    }
    if (plan->fs_arg_encoder)
    {
        id<MTLArgumentEncoder> enc = (__bridge id<MTLArgumentEncoder>)plan->fs_arg_encoder;
        id<MTLBuffer>          ab = mel_gpu__mtl_encode_arg_buffer(cmd, enc, plan->fs_arg_encoded_length, plan, MTLRenderStageFragment);
        if (!ab)
            return false;
        [cmd->encoder setFragmentBuffer:ab offset:0 atIndex:MEL_GPU_METAL_PUSH_CONSTANT_INDEX];
    }
    return true;
}

void mel_gpu_cmd_dispatch(Mel_Gpu_Command_List* cmd, u32 groups_x, u32 groups_y, u32 groups_z)
{
    if (!cmd->compute_encoder || !cmd->has_compute_pipeline)
    {
        mel_log_error("gpu", "cmd_dispatch: no active compute encoder with a bound compute pipeline (call cmd_bind_pipeline on a compute pipeline first)");
        return;
    }
    Mel_Gpu_Pipeline_Obj plan;
    if (mel_gpu__compute_argbuffer_plan(cmd, &plan))
    {
        if (!mel_gpu__build_compute_argbuffer(cmd, &plan))
            return;
    }
    MTLSize groups = MTLSizeMake(groups_x ? groups_x : 1, groups_y ? groups_y : 1, groups_z ? groups_z : 1);
    [cmd->compute_encoder dispatchThreadgroups:groups threadsPerThreadgroup:cmd->compute_threadgroup];
}

void mel_gpu_cmd_dispatch_indirect(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer args, usize offset)
{
    if (!cmd->compute_encoder || !cmd->has_compute_pipeline)
    {
        mel_log_error("gpu", "cmd_dispatch_indirect: no active compute encoder with a bound compute pipeline (call cmd_bind_pipeline on a compute pipeline first)");
        return;
    }
    id<MTLBuffer> ab = nil;
    if (!mel_gpu__buffer_get(cmd->dev, args, &ab))
    {
        mel_log_error("gpu", "cmd_dispatch_indirect: args buffer is not a live handle");
        return;
    }
    [cmd->compute_encoder dispatchThreadgroupsWithIndirectBuffer:ab indirectBufferOffset:offset threadsPerThreadgroup:cmd->compute_threadgroup];
}
