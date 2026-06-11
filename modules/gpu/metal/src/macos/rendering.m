#include "mtl_backend.h"

#include <log/log.h>

static MTLLoadAction mel_gpu__load_action(Mel_Gpu_Load_Op op)
{
    switch (op)
    {
    case MEL_GPU_LOAD_LOAD:
        return MTLLoadActionLoad;
    case MEL_GPU_LOAD_DONT_CARE:
        return MTLLoadActionDontCare;
    case MEL_GPU_LOAD_CLEAR:
    default:
        return MTLLoadActionClear;
    }
}

static MTLStoreAction mel_gpu__store_action(Mel_Gpu_Store_Op op)
{
    return op == MEL_GPU_STORE_DONT_CARE ? MTLStoreActionDontCare : MTLStoreActionStore;
}

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Command_List* cmd, Mel_Gpu_Rendering_Opt opt)
{
    mel_assert(cmd);
    if (!cmd->cb)
    {
        mel_log_error("gpu", "cmd_begin_rendering: command list has no command buffer");
        return;
    }

    mel_gpu__cmd_end_active_encoder(cmd);

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];

    for (u32 i = 0; i < opt.color_count; i++)
    {
        Mel_Gpu_Texture_View_Obj v;
        if (!mel_gpu__texture_view_get(cmd->dev, opt.colors[i].view, &v))
        {
            mel_log_error("gpu", "cmd_begin_rendering: color attachment %u is not a live view", i);
            continue;
        }
        rp.colorAttachments[i].texture = (__bridge id<MTLTexture>)v.view;
        rp.colorAttachments[i].loadAction = mel_gpu__load_action(opt.colors[i].load);
        rp.colorAttachments[i].storeAction = mel_gpu__store_action(opt.colors[i].store);
        Mel_Gpu_Color c = opt.colors[i].clear;
        rp.colorAttachments[i].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
    }

    if (opt.depth)
    {
        Mel_Gpu_Texture_View_Obj v;
        if (mel_gpu__texture_view_get(cmd->dev, opt.depth->view, &v))
        {
            rp.depthAttachment.texture = (__bridge id<MTLTexture>)v.view;
            rp.depthAttachment.loadAction = mel_gpu__load_action(opt.depth->load);
            rp.depthAttachment.storeAction = mel_gpu__store_action(opt.depth->store);
            rp.depthAttachment.clearDepth = opt.depth->clear_depth;
        }
    }

    cmd->encoder = [cmd->cb renderCommandEncoderWithDescriptor:rp];
    cmd->has_pipeline = false;

    MTLViewport vp = { 0.0, 0.0, (double)opt.width, (double)opt.height, 0.0, 1.0 };
    [cmd->encoder setViewport:vp];
}

void mel_gpu_cmd_end_rendering(Mel_Gpu_Command_List* cmd)
{
    if (cmd && cmd->encoder)
    {
        [cmd->encoder endEncoding];
        cmd->encoder = nil;
    }
}
