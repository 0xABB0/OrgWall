#include "wgpu_backend.h"

#include <log/log.h>

#include <string.h>

WGPUStringView mel_gpu__sv(const char* s)
{
    if (!s)
        return (WGPUStringView){ .data = NULL, .length = WGPU_STRLEN };
    return (WGPUStringView){ .data = s, .length = strlen(s) };
}

WGPUTextureFormat mel_gpu__wgpu_format(Mel_Gpu_Format fmt)
{
    switch (fmt)
    {
    case MEL_GPU_FORMAT_BGRA8_UNORM:
        return WGPUTextureFormat_BGRA8Unorm;
    case MEL_GPU_FORMAT_BGRA8_SRGB:
        return WGPUTextureFormat_BGRA8UnormSrgb;
    case MEL_GPU_FORMAT_RGBA8_UNORM:
        return WGPUTextureFormat_RGBA8Unorm;
    case MEL_GPU_FORMAT_RGBA8_SRGB:
        return WGPUTextureFormat_RGBA8UnormSrgb;
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return WGPUTextureFormat_RG32Float;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return WGPUTextureFormat_RGBA32Float;
    case MEL_GPU_FORMAT_D32_FLOAT:
        return WGPUTextureFormat_Depth32Float;
    case MEL_GPU_FORMAT_D24_UNORM_S8_UINT:
        return WGPUTextureFormat_Depth24PlusStencil8;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
    case MEL_GPU_FORMAT_UNDEFINED:
    default:
        return WGPUTextureFormat_Undefined;
    }
}

Mel_Gpu_Format mel_gpu__wgpu_format_to_mel(WGPUTextureFormat fmt)
{
    switch (fmt)
    {
    case WGPUTextureFormat_BGRA8Unorm:
        return MEL_GPU_FORMAT_BGRA8_UNORM;
    case WGPUTextureFormat_BGRA8UnormSrgb:
        return MEL_GPU_FORMAT_BGRA8_SRGB;
    case WGPUTextureFormat_RGBA8Unorm:
        return MEL_GPU_FORMAT_RGBA8_UNORM;
    case WGPUTextureFormat_RGBA8UnormSrgb:
        return MEL_GPU_FORMAT_RGBA8_SRGB;
    case WGPUTextureFormat_RG32Float:
        return MEL_GPU_FORMAT_RG32_FLOAT;
    case WGPUTextureFormat_RGBA32Float:
        return MEL_GPU_FORMAT_RGBA32_FLOAT;
    case WGPUTextureFormat_Depth32Float:
        return MEL_GPU_FORMAT_D32_FLOAT;
    case WGPUTextureFormat_Depth24PlusStencil8:
        return MEL_GPU_FORMAT_D24_UNORM_S8_UINT;
    default:
        return MEL_GPU_FORMAT_UNDEFINED;
    }
}

void mel_gpu__track_enter(Mel_Gpu_Device* dev, const void* object, Mel_Gpu_Concurrency cls)
{
    if (dev->tracker)
        mel_gpu_thread_tracker_enter(dev->tracker, object, cls);
}

void mel_gpu__track_exit(Mel_Gpu_Device* dev, const void* object)
{
    if (dev->tracker)
        mel_gpu_thread_tracker_exit(dev->tracker, object);
}

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj)
{
    mel_mutex_lock(&dev->obj_lock);
    Mel_SlotMap_Handle h = mel_slotmap_insert(&t->map, obj);
    mel_mutex_unlock(&dev->obj_lock);
    return h;
}

void* mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    void* p = mel_slotmap_get(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return p;
}

bool mel_gpu__table_get_copy(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h, void* out)
{
    mel_mutex_lock(&dev->obj_lock);
    void* p = mel_slotmap_get(&t->map, h);
    if (p)
        memcpy(out, p, t->map.item_size);
    mel_mutex_unlock(&dev->obj_lock);
    return p != NULL;
}

bool mel_gpu__table_alive(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool alive = mel_slotmap_get(&t->map, h) != NULL;
    mel_mutex_unlock(&dev->obj_lock);
    return alive;
}

bool mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h)
{
    mel_mutex_lock(&dev->obj_lock);
    bool ok = mel_slotmap_remove(&t->map, h);
    mel_mutex_unlock(&dev->obj_lock);
    return ok;
}

u64 mel_gpu__submit_serial_next(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->submit_lock);
    u64 s = ++dev->submit_serial;
    mel_mutex_unlock(&dev->submit_lock);
    return s;
}

void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial)
{
    mel_mutex_lock(&dev->submit_lock);
    if (serial > dev->submit_completed)
        dev->submit_completed = serial;
    mel_mutex_unlock(&dev->submit_lock);
}

void mel_gpu__instance_pump_tick(void* user)
{
    Mel_Gpu_Device* dev = (Mel_Gpu_Device*)user;
    if (dev && dev->wgpu_instance)
        wgpuInstanceProcessEvents(dev->wgpu_instance);
}

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, Mel_Gpu_Buffer_Obj* out)
{
    return mel_gpu__table_get_copy(dev, &dev->buffers, buf.slot, out);
}

bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj* out)
{
    return mel_gpu__table_get_copy(dev, &dev->textures, tex.slot, out);
}

bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj* out)
{
    return mel_gpu__table_get_copy(dev, &dev->texture_views, view.slot, out);
}
