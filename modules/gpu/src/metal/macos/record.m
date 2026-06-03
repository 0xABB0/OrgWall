#include "mtl_backend.h"

#include <log/log.h>

static void mel_gpu__warn_unsupported(Mel_Gpu_Command_List* cmd, const char* what)
{
    if (cmd && !cmd->warned_unsupported)
    {
        cmd->warned_unsupported = true;
        mel_log_warn("gpu", "metal backend: %s is not implemented this round; the call is a loud no-op (clear-and-present only)", what);
    }
    else if (!cmd)
    {
        mel_log_warn("gpu", "metal backend: %s is not implemented this round", what);
    }
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
    (void)tex;
    (void)subresource;
    (void)dst;
    mel_gpu__warn_unsupported(cmd, "cmd_copy_texture_to_buffer");
}

void mel_gpu_cmd_copy_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer src, Mel_Gpu_Buffer dst, usize bytes)
{
    (void)src;
    (void)dst;
    (void)bytes;
    mel_gpu__warn_unsupported(cmd, "cmd_copy_buffer");
}

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe)
{
    (void)pipe;
    mel_gpu__warn_unsupported(cmd, "cmd_bind_pipeline (no pipeline lowering this round)");
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf)
{
    (void)slot;
    (void)buf;
    mel_gpu__warn_unsupported(cmd, "cmd_bind_vertex_buffer");
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Index_Type type)
{
    (void)buf;
    (void)type;
    mel_gpu__warn_unsupported(cmd, "cmd_bind_index_buffer");
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 bytes, const void* data)
{
    (void)offset;
    (void)bytes;
    (void)data;
    mel_gpu__warn_unsupported(cmd, "cmd_push_constants");
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    (void)vertex_count;
    (void)instance_count;
    mel_gpu__warn_unsupported(cmd, "cmd_draw (no pipeline lowering this round)");
}

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count)
{
    (void)index_count;
    (void)instance_count;
    mel_gpu__warn_unsupported(cmd, "cmd_draw_indexed");
}

void mel_gpu_cmd_dispatch(Mel_Gpu_Command_List* cmd, u32 groups_x, u32 groups_y, u32 groups_z)
{
    (void)groups_x;
    (void)groups_y;
    (void)groups_z;
    mel_gpu__warn_unsupported(cmd, "cmd_dispatch");
}

void mel_gpu_cmd_dispatch_indirect(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer args, usize offset)
{
    (void)args;
    (void)offset;
    mel_gpu__warn_unsupported(cmd, "cmd_dispatch_indirect");
}
