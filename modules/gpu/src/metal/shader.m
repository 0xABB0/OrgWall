#include "metal.h"

Mel_Gpu_Shader* mel_gpu_shader_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt)
{
    if (!dev || !opt.metal_source) return NULL;

    NSError*       err = nil;
    id<MTLLibrary> lib = [dev->mtl newLibraryWithSource:@(opt.metal_source) options:nil error:&err];
    if (!lib) {
        if (err) NSLog(@"mel_gpu: metal shader compile failed: %@", err.localizedDescription);
        return NULL;
    }

    Mel_Gpu_Shader* sh = calloc(1, sizeof *sh);
    if (!sh) return NULL;
    sh->library = lib;
    if (opt.vertex_entry)   sh->vertex_fn   = [lib newFunctionWithName:@(opt.vertex_entry)];
    if (opt.fragment_entry) sh->fragment_fn = [lib newFunctionWithName:@(opt.fragment_entry)];

    if ((opt.vertex_entry && !sh->vertex_fn) || (opt.fragment_entry && !sh->fragment_fn)) {
        NSLog(@"mel_gpu: metal shader entry point not found");
        mel_gpu_shader_destroy(sh);
        return NULL;
    }
    return sh;
}

void mel_gpu_shader_destroy(Mel_Gpu_Shader* sh)
{
    if (!sh) return;
    sh->vertex_fn   = nil;
    sh->fragment_fn = nil;
    sh->library     = nil;
    free(sh);
}
