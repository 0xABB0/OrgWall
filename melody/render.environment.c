#include "render.environment.h"
#include "collection.slotmap.h"
#include "allocator.heap.h"

#include <assert.h>

static Mel_SlotMap s_environments;
static bool s_environments_initialized;

__attribute__((constructor))
static void mel__render_environment_init(void)
{
    mel_slotmap_init(&s_environments, mel_alloc_heap(),
        .item_size = sizeof(Mel_Render_Environment),
        .initial_capacity = 8);
    s_environments_initialized = true;
}

__attribute__((destructor))
static void mel__render_environment_shutdown(void)
{
    if (!s_environments_initialized)
        return;

    mel_slotmap_free(&s_environments);
    s_environments_initialized = false;
}

Mel_Render_Environment_Handle mel_render_environment_create_constant(Mel_Vec4 radiance)
{
    assert(s_environments_initialized);

    Mel_Render_Environment env = {
        .type = MEL_RENDER_ENVIRONMENT_CONSTANT,
        .constant_radiance = radiance,
    };

    return (Mel_Render_Environment_Handle){
        .handle = mel_slotmap_insert(&s_environments, &env),
    };
}

void mel_render_environment_destroy(Mel_Render_Environment_Handle handle)
{
    if (!s_environments_initialized)
        return;
    if (!mel_slotmap_alive(&s_environments, handle.handle))
        return;
    mel_slotmap_remove(&s_environments, handle.handle);
}

bool mel_render_environment_alive(Mel_Render_Environment_Handle handle)
{
    if (!s_environments_initialized)
        return false;
    return mel_slotmap_alive(&s_environments, handle.handle);
}

Mel_Render_Environment* mel_render_environment_get(Mel_Render_Environment_Handle handle)
{
    assert(s_environments_initialized);
    Mel_Render_Environment* env = mel_slotmap_get(&s_environments, handle.handle);
    assert(env != nullptr);
    return env;
}

void mel_render_environment_set_constant(Mel_Render_Environment_Handle handle, Mel_Vec4 radiance)
{
    Mel_Render_Environment* env = mel_render_environment_get(handle);
    assert(env->type == MEL_RENDER_ENVIRONMENT_CONSTANT);
    env->constant_radiance = radiance;
}
