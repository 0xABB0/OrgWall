#include "sprite.material.h"
#include "render.material_base.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "str8.h"
#include "async.job.h"

#include <assert.h>

static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Device* s_dev;
static Mel_Material_Base_Id s_material_id = MEL_MATERIAL_BASE_ID_INVALID;

Mel_Material_Base_Id mel_sprite_material_id(void)
{
    assert(s_material_id != MEL_MATERIAL_BASE_ID_INVALID);
    return s_material_id;
}

static void mel__sprite_material_compile(void* data)
{
    (void)data;
    mel_gpu_shader_load(&s_shader, .path = S8("shaders/sprite_2d.slang"), .dev = s_dev);
    mel_material_base_set_shader(s_material_id, &s_shader);
}

static void mel__sprite_material_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__sprite_material_compile, NULL);
}

static void mel__sprite_material_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    if (s_shader._vertex != nullptr)
        mel_gpu_shader_shutdown(&s_shader, s_dev);
}

static void mel__sprite_material_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__sprite_material_on_gpu_ready, NULL);
    mel_event_channel_on(&mel_shutdown_begin, mel__sprite_material_on_shutdown, NULL);

    s_material_id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("sprite_2d"),
        .param_size = 0,
        .compat = MEL_COMPAT_2D,
    });
}

__attribute__((constructor))
static void mel__sprite_material_register(void)
{
    mel__boot_register_wire(mel__sprite_material_wire);
}
