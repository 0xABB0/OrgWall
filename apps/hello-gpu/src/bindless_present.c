#include <stddef.h>

#include "bindless_present.h"

static const char BINDLESS_PRESENT_SLANG[] = {
#embed "shaders/slang/bindless_present.slang"
    , 0
};

bool bindless_present_init(Bindless_Present* bp, Mel_Gpu_Device* dev, Mel_Gpu_Format color_format)
{
    bp->dev = dev;

    Mel_Gpu_Pipeline_From_Slang_Result pipe = mel_gpu_pipeline_create_from_slang(dev,
                                                                                 .source = BINDLESS_PRESENT_SLANG,
                                                                                 .vertex_entry = "vs_main",
                                                                                 .fragment_entry = "fs_main",
                                                                                 .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                 .cull = MEL_GPU_CULL_NONE,
                                                                                 .color_format = color_format,
                                                                                 .bindless = true,
                                                                                 .name = "bindless-present");
    if (mel_gpu_failed(pipe.status))
        return false;
    bp->pipeline = pipe.value;
    bp->shader = pipe.shader;

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "bindless-present-sampler");
    if (mel_gpu_failed(smp.status))
        return false;
    bp->sampler = smp.value;
    return true;
}

void bindless_present_blit(Bindless_Present* bp, Mel_Gpu_Command_List* cmd, u32 tex_slot, Mel_Gpu_Color clear)
{
    struct
    {
        u32 tex;
        u32 smp;
    } root = { tex_slot, mel_gpu_sampler_bindless_slot(bp->dev, bp->sampler) };

    mel_gpu_cmd_begin_pass(cmd, clear);
    mel_gpu_cmd_bind_pipeline(cmd, bp->pipeline);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

void bindless_present_teardown(Bindless_Present* bp)
{
    mel_gpu_sampler_destroy(bp->dev, bp->sampler);
    mel_gpu_pipeline_destroy(bp->dev, bp->pipeline);
    mel_gpu_shader_destroy(bp->dev, bp->shader);
}
