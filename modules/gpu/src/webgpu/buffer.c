#include "webgpu_backend.h"

Mel_Gpu_Buffer* mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt)
{
    if (!dev || opt.size == 0) return NULL;

    WGPUBufferUsage usage = WGPUBufferUsage_CopyDst;
    if (opt.usage & MEL_GPU_BUFFER_VERTEX)  usage |= WGPUBufferUsage_Vertex;
    if (opt.usage & MEL_GPU_BUFFER_INDEX)   usage |= WGPUBufferUsage_Index;
    if (opt.usage & MEL_GPU_BUFFER_UNIFORM) usage |= WGPUBufferUsage_Uniform;

    usize size = (opt.size + 3u) & ~(usize)3u; // WebGPU buffer sizes are 4-byte aligned

    WGPUBufferDescriptor d = {
        .usage            = usage,
        .size             = size,
        .mappedAtCreation = opt.data != NULL,
    };
    WGPUBuffer b = wgpuDeviceCreateBuffer(dev->device, &d);
    if (!b) return NULL;

    if (opt.data) {
        void* p = wgpuBufferGetMappedRange(b, 0, size);
        if (p) memcpy(p, opt.data, opt.size);
        wgpuBufferUnmap(b);
    }

    Mel_Gpu_Buffer* buf = calloc(1, sizeof *buf);
    if (!buf) { wgpuBufferRelease(b); return NULL; }
    buf->device       = dev;
    buf->buf          = b;
    buf->size         = size;
    buf->host_visible = (opt.memory != MEL_GPU_MEMORY_GPU_ONLY);
    return buf;
}

void mel_gpu_buffer_destroy(Mel_Gpu_Buffer* buf)
{
    if (!buf) return;
    if (buf->buf) wgpuBufferRelease(buf->buf);
    free(buf);
}

void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf)
{
    (void)buf;
    return NULL; // WebGPU mapping is asynchronous; use mel_gpu_buffer_write
}

void mel_gpu_buffer_write(Mel_Gpu_Buffer* buf, const void* data, usize size)
{
    if (!buf || !data || size == 0) return;
    if (size > buf->size) size = buf->size;
    size &= ~(usize)3u; // wgpuQueueWriteBuffer requires a 4-byte-multiple size
    if (size == 0) return;
    wgpuQueueWriteBuffer(buf->device->queue, buf->buf, 0, data, size);
}
