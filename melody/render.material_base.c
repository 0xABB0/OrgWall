#include "render.material_base.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "core.engine.h"
#include "event.channel.h"
#include "boot.registry.h"

#include <string.h>

static Mel_Material_Base s_bases[MEL_MATERIAL_BASE_MAX];
static u32 s_base_count;

Mel_Material_Base_Id mel_material_base_register(const Mel_Material_Base_Desc* desc)
{
    assert(desc != nullptr);
    assert(desc->name.len > 0);
    assert(desc->param_size > 0);
    assert(s_base_count < MEL_MATERIAL_BASE_MAX);

    for (u32 i = 0; i < s_base_count; i++)
        assert(!str8_equals(s_bases[i].name, desc->name));

    Mel_Material_Base_Id id = s_base_count++;
    Mel_Material_Base* base = &s_bases[id];

    *base = (Mel_Material_Base){0};
    base->name = desc->name;
    base->param_size = desc->param_size;
    base->compat = desc->compat;
    base->alloc = mel_alloc_heap();

    u32 initial_cap = 16;
    base->params = mel_alloc(base->alloc, (usize)initial_cap * desc->param_size);
    memset(base->params, 0, (usize)initial_cap * desc->param_size);
    base->instance_count = 0;
    base->instance_capacity = initial_cap;

    return id;
}

Mel_Material_Base_Id mel_material_base_find(str8 name)
{
    for (u32 i = 0; i < s_base_count; i++)
    {
        if (str8_equals(s_bases[i].name, name))
            return i;
    }
    return MEL_MATERIAL_BASE_ID_INVALID;
}

Mel_Material_Base* mel_material_base_get(Mel_Material_Base_Id id)
{
    assert(id < s_base_count);
    return &s_bases[id];
}

u32 mel_material_base_count(void)
{
    return s_base_count;
}

Mel_Material_Instance_Id mel_material_base_alloc_instance(Mel_Material_Base_Id base_id, const void* initial_params)
{
    assert(base_id < s_base_count);
    assert(initial_params != nullptr);

    Mel_Material_Base* base = &s_bases[base_id];

    if (base->instance_count >= base->instance_capacity)
    {
        u32 new_cap = base->instance_capacity * 2;
        u8* new_params = mel_alloc(base->alloc, (usize)new_cap * base->param_size);
        memcpy(new_params, base->params, (usize)base->instance_count * base->param_size);
        mel_dealloc(base->alloc, base->params);
        base->params = new_params;
        base->instance_capacity = new_cap;
    }

    Mel_Material_Instance_Id id = base->instance_count++;
    memcpy(base->params + (usize)id * base->param_size, initial_params, base->param_size);
    base->param_buffer_dirty = true;

    return id;
}

void mel_material_base_free_instance(Mel_Material_Base_Id base_id, Mel_Material_Instance_Id instance_id)
{
    assert(base_id < s_base_count);
    Mel_Material_Base* base = &s_bases[base_id];
    assert(instance_id < base->instance_count);

    u32 last = base->instance_count - 1;
    if (instance_id != last)
    {
        memcpy(base->params + (usize)instance_id * base->param_size,
               base->params + (usize)last * base->param_size,
               base->param_size);
    }
    base->instance_count--;
    base->param_buffer_dirty = true;
}

void mel_material_base_set_params(Mel_Material_Base_Id base_id, Mel_Material_Instance_Id instance_id, const void* params)
{
    assert(base_id < s_base_count);
    assert(params != nullptr);

    Mel_Material_Base* base = &s_bases[base_id];
    assert(instance_id < base->instance_count);

    memcpy(base->params + (usize)instance_id * base->param_size, params, base->param_size);
    base->param_buffer_dirty = true;
}

const void* mel_material_base_get_params(Mel_Material_Base_Id base_id, Mel_Material_Instance_Id instance_id)
{
    assert(base_id < s_base_count);
    Mel_Material_Base* base = &s_bases[base_id];
    assert(instance_id < base->instance_count);

    return base->params + (usize)instance_id * base->param_size;
}

void mel_material_base_upload_dirty(Mel_Material_Base_Id base_id, Mel_Gpu_Device* dev)
{
    assert(base_id < s_base_count);
    assert(dev != nullptr);

    Mel_Material_Base* base = &s_bases[base_id];
    if (!base->param_buffer_dirty || base->instance_count == 0)
        return;

    u64 needed = (u64)base->instance_count * base->param_size;

    if (base->param_buffer._handle == nullptr || base->param_buffer.size < needed)
    {
        if (base->param_buffer._handle != nullptr)
            mel_gpu_buffer_shutdown(&base->param_buffer, dev);

        u64 buf_size = needed;
        if (buf_size < 256) buf_size = 256;

        mel_gpu_buffer_init(&base->param_buffer, dev,
            .size = buf_size,
            .usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
            .map_on_create = true);
    }

    mel_gpu_buffer_upload(&base->param_buffer, dev, base->params, needed, 0);
    base->param_buffer_dirty = false;
}

Mel_Gpu_Buffer* mel_material_base_param_buffer(Mel_Material_Base_Id base_id)
{
    assert(base_id < s_base_count);
    return &s_bases[base_id].param_buffer;
}

void mel__material_base_init_gpu(Mel_Material_Base_Id base_id, Mel_Gpu_Device* dev)
{
    assert(base_id < s_base_count);
    (void)dev;
    s_bases[base_id].shader_ready = true;
}

static void mel__material_base_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    for (u32 i = 0; i < s_base_count; i++)
    {
        if (s_bases[i].param_buffer._handle != nullptr)
            mel_gpu_buffer_shutdown(&s_bases[i].param_buffer, dev);
        if (s_bases[i].params)
            mel_dealloc(s_bases[i].alloc, s_bases[i].params);
        s_bases[i] = (Mel_Material_Base){0};
    }
    s_base_count = 0;
}

static void mel__material_base_wire(void)
{
    mel_event_channel_on(&mel_shutdown_begin, mel__material_base_on_shutdown, nullptr);
}

__attribute__((constructor))
static void mel__material_base_register_boot(void)
{
    mel__boot_register_wire(mel__material_base_wire);
}
