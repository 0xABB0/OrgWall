#include <string.h>

#include "metal.h"

Mel_Gpu_Buffer* mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    if (!dev || opt.size == 0) return NULL;

    Mel_Gpu_Buffer* buf = calloc(1, sizeof *buf);
    if (!buf) return NULL;
    buf->size         = opt.size;
    buf->host_visible = (opt.memory != MEL_GPU_MEMORY_GPU_ONLY);

    MTLResourceOptions res = buf->host_visible ? MTLResourceStorageModeShared
                                               : MTLResourceStorageModePrivate;

    if (buf->host_visible) {
        buf->mtl = opt.data ? [dev->mtl newBufferWithBytes:opt.data length:opt.size options:res]
                            : [dev->mtl newBufferWithLength:opt.size options:res];
    } else {
        buf->mtl = [dev->mtl newBufferWithLength:opt.size options:res];
        if (buf->mtl && opt.data) {
            id<MTLBuffer> staging = [dev->mtl newBufferWithBytes:opt.data length:opt.size
                                                         options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer>      cb   = [dev->queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromBuffer:staging sourceOffset:0 toBuffer:buf->mtl destinationOffset:0 size:opt.size];
            [blit endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }
    }

    if (!buf->mtl) { free(buf); return NULL; }
    return buf;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Buffer* buf)
{
    if (!buf) return;
    buf->mtl = nil;
    free(buf);
}

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf)
{
    if (!buf || !buf->host_visible) return NULL;
    return [buf->mtl contents];
}

void mel_gpu_buffer_write(Mel_Gpu_Buffer* buf, const void* data, usize size)
{
    if (!buf || !buf->host_visible || !data || size == 0) return;
    if (size > buf->size) size = buf->size;
    memcpy([buf->mtl contents], data, size);
}
