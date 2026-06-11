#include "wgpu_backend.h"

#include <log/log.h>

static WGPULoadOp mel_gpu__load_op(Mel_Gpu_Load_Op op)
{
    return op == MEL_GPU_LOAD_LOAD ? WGPULoadOp_Load : WGPULoadOp_Clear;
}

static WGPUStoreOp mel_gpu__store_op(Mel_Gpu_Store_Op op)
{
    return op == MEL_GPU_STORE_DONT_CARE ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
}

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Command_List* cmd, Mel_Gpu_Rendering_Opt opt)
{
    mel_assert(cmd);
    if (!cmd->encoder)
    {
        mel_log_error("gpu", "cmd_begin_rendering: command list has no command encoder");
        return;
    }
    if (opt.color_count > 8)
    {
        mel_log_error("gpu", "cmd_begin_rendering: color_count %u exceeds 8", opt.color_count);
        return;
    }

    WGPURenderPassColorAttachment colors[8] = { 0 };
    for (u32 i = 0; i < opt.color_count; i++)
    {
        Mel_Gpu_Texture_View_Obj v;
        if (!mel_gpu__texture_view_get(cmd->dev, opt.colors[i].view, &v))
        {
            mel_log_error("gpu", "cmd_begin_rendering: color attachment %u is not a live view", i);
            return;
        }
        colors[i].view = v.wgpu;
        colors[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colors[i].loadOp = mel_gpu__load_op(opt.colors[i].load);
        colors[i].storeOp = mel_gpu__store_op(opt.colors[i].store);
        Mel_Gpu_Color c = opt.colors[i].clear;
        colors[i].clearValue = (WGPUColor){ c.r, c.g, c.b, c.a };
        if (opt.colors[i].resolve_view.slot.index)
        {
            Mel_Gpu_Texture_View_Obj rv;
            if (mel_gpu__texture_view_get(cmd->dev, opt.colors[i].resolve_view, &rv))
                colors[i].resolveTarget = rv.wgpu;
        }
    }

    WGPURenderPassDepthStencilAttachment depth = { 0 };
    bool                                 have_depth = false;
    if (opt.depth)
    {
        Mel_Gpu_Texture_View_Obj dv;
        if (mel_gpu__texture_view_get(cmd->dev, opt.depth->view, &dv))
        {
            depth.view = dv.wgpu;
            depth.depthLoadOp = mel_gpu__load_op(opt.depth->load);
            depth.depthStoreOp = mel_gpu__store_op(opt.depth->store);
            depth.depthClearValue = opt.depth->clear_depth;
            depth.stencilLoadOp = WGPULoadOp_Undefined;
            depth.stencilStoreOp = WGPUStoreOp_Undefined;
            have_depth = true;
        }
    }

    WGPURenderPassDescriptor rp = {
        .colorAttachmentCount = opt.color_count,
        .colorAttachments = colors,
        .depthStencilAttachment = have_depth ? &depth : NULL,
    };
    cmd->pass = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &rp);
    wgpuRenderPassEncoderSetViewport(cmd->pass, 0.0f, 0.0f, (float)opt.width, (float)opt.height, 0.0f, 1.0f);
}

void mel_gpu_cmd_end_rendering(Mel_Gpu_Command_List* cmd)
{
    if (cmd && cmd->pass)
    {
        wgpuRenderPassEncoderEnd(cmd->pass);
        wgpuRenderPassEncoderRelease(cmd->pass);
        cmd->pass = NULL;
    }
}
