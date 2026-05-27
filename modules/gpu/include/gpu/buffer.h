#pragma once

#include <gpu/types.h>

typedef struct {
    usize                size;
    Mel_Gpu_Buffer_Usage usage;
    Mel_Gpu_Memory       memory;
    const void*          data; // optional initial contents (size bytes)
} Mel_Gpu_Buffer_Opt;

Mel_Gpu_Buffer* mel_gpu_buffer_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Buffer_Opt opt);
#define mel_gpu_buffer_create(dev, ...) \
    mel_gpu_buffer_create_opt((dev), (Mel_Gpu_Buffer_Opt){__VA_ARGS__})

void  mel_gpu_buffer_destroy(Mel_Gpu_Buffer* buf);

// CPU pointer for an UPLOAD buffer; NULL for GPU_ONLY.
void* mel_gpu_buffer_map(Mel_Gpu_Buffer* buf);

// Replace the first `size` bytes of an UPLOAD buffer with `data`. Portable across
// every backend; `size` is clamped to the buffer's capacity. Writing a buffer the
// GPU may still be reading is a hazard, so cycle several buffers for per-frame data.
void  mel_gpu_buffer_write(Mel_Gpu_Buffer* buf, const void* data, usize size);
