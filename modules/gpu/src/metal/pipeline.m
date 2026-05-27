#include "metal.h"

static MTLCullMode mel_gpu__mtl_cull(Mel_Gpu_Cull c)
{
    switch (c) {
        case MEL_GPU_CULL_FRONT: return MTLCullModeFront;
        case MEL_GPU_CULL_BACK:  return MTLCullModeBack;
        default:                 return MTLCullModeNone;
    }
}

Mel_Gpu_Pipeline* mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    if (!dev || !opt.shader) return NULL;

    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = opt.shader->vertex_fn;
    desc.fragmentFunction = opt.shader->fragment_fn;
    desc.colorAttachments[0].pixelFormat = mel_gpu__mtl_pixel_format(opt.color_format);

    if (opt.vertex_layout && opt.vertex_layout_count > 0) {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (u32 i = 0; i < opt.vertex_layout_count; i++) {
            const Mel_Gpu_Vertex_Element* e = &opt.vertex_layout[i];
            vd.attributes[e->location].format      = mel_gpu__mtl_vertex_format(e->format);
            vd.attributes[e->location].offset      = e->offset;
            vd.attributes[e->location].bufferIndex = 0;
        }
        vd.layouts[0].stride = opt.vertex_stride;
        desc.vertexDescriptor = vd;
    }

    NSError*                   err   = nil;
    id<MTLRenderPipelineState> state = [dev->mtl newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!state) {
        if (err) NSLog(@"mel_gpu: metal pipeline creation failed: %@", err.localizedDescription);
        return NULL;
    }

    Mel_Gpu_Pipeline* pipe = calloc(1, sizeof *pipe);
    if (!pipe) return NULL;
    pipe->state     = state;
    pipe->primitive = mel_gpu__mtl_primitive(opt.topology);
    pipe->cull      = mel_gpu__mtl_cull(opt.cull);
    return pipe;
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Pipeline* pipe)
{
    if (!pipe) return;
    pipe->state = nil;
    free(pipe);
}
