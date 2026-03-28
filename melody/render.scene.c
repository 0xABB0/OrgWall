#include "render.scene.h"
#include "collection.array.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

typedef struct {
    const Mel_Render_Pipeline_Type* type;
    Mel_Render_Pipeline_Scene* scene;
    u64 last_sync_serial;
} Mel_Render_Scene_Pipeline_Entry;

typedef Mel_Array(Mel_Render_Source*) Mel_Render_Source_Array;
typedef Mel_Array(Mel_Render_Scene_Pipeline_Entry) Mel_Render_Scene_Pipeline_Array;
typedef Mel_Array(Mel_Render_Scene_Directional_Light) Mel_Render_Scene_Directional_Light_Array;
typedef Mel_Array(Mel_Render_Scene_Point_Light) Mel_Render_Scene_Point_Light_Array;

struct Mel_Render_Scene {
    Mel_Render_Manager manager;
    Mel_Render_Source_Array sources;
    Mel_Render_Scene_Pipeline_Array pipelines;
    Mel_Render_Environment_Handle environment;
    Mel_Render_Scene_Directional_Light_Array directional_lights;
    Mel_Render_Scene_Point_Light_Array point_lights;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    u64 sync_serial;
};

Mel_Render_Scene* mel_render_scene_create_opt(Mel_Render_Scene_Opt opt)
{
    assert(opt.dev != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Render_Scene* scene = mel_alloc(alloc, sizeof(Mel_Render_Scene));
    *scene = (Mel_Render_Scene){0};
    scene->dev = opt.dev;
    scene->alloc = alloc;
    scene->sync_serial = 1;

    mel_array_init(&scene->sources, alloc);
    mel_array_init(&scene->pipelines, alloc);
    mel_array_init(&scene->directional_lights, alloc);
    mel_array_init(&scene->point_lights, alloc);
    mel_mgr_init(&scene->manager,
        .alloc = alloc,
        .initial_capacity = opt.initial_capacity);

    return scene;
}

void mel_render_scene_destroy(Mel_Render_Scene* scene)
{
    assert(scene != nullptr);

    for (usize i = 0; i < scene->sources.count; i++)
        scene->sources.items[i]->scene = nullptr;

    for (usize i = 0; i < scene->pipelines.count; i++)
        mel_pipeline_scene_destroy(scene->pipelines.items[i].scene);

    mel_array_free(&scene->point_lights);
    mel_array_free(&scene->directional_lights);
    mel_array_free(&scene->pipelines);
    mel_array_free(&scene->sources);
    mel_mgr_shutdown(&scene->manager);
    mel_dealloc(scene->alloc, scene);
}

void mel_render_scene_attach_source(Mel_Render_Scene* scene, Mel_Render_Source* source)
{
    assert(scene != nullptr);
    assert(source != nullptr);
    assert(source->scene == nullptr || source->scene == scene);

    if (source->scene == scene)
        return;

    source->scene = scene;
    mel_array_push(&scene->sources, source);
}

void mel_render_scene_detach_source(Mel_Render_Scene* scene, Mel_Render_Source* source)
{
    assert(scene != nullptr);
    assert(source != nullptr);

    for (usize i = 0; i < scene->sources.count; i++)
    {
        if (scene->sources.items[i] != source)
            continue;

        memmove(&scene->sources.items[i],
                &scene->sources.items[i + 1],
                (scene->sources.count - i - 1) * sizeof(scene->sources.items[0]));
        scene->sources.count--;
        source->scene = nullptr;
        return;
    }
}

void mel_render_scene_sync(Mel_Render_Scene* scene)
{
    assert(scene != nullptr);

    for (usize i = 0; i < scene->sources.count; i++)
        mel_render_source_sync(scene->sources.items[i], &scene->manager);

    scene->sync_serial++;
}

Mel_Render_Manager* mel_render_scene_manager(Mel_Render_Scene* scene)
{
    assert(scene != nullptr);
    return &scene->manager;
}

void mel_render_scene_set_environment(Mel_Render_Scene* scene,
                                      Mel_Render_Environment_Handle environment)
{
    assert(scene != nullptr);
    scene->environment = environment;
}

Mel_Render_Environment_Handle mel_render_scene_environment(Mel_Render_Scene* scene)
{
    assert(scene != nullptr);
    return scene->environment;
}

void mel_render_scene_clear_directional_lights(Mel_Render_Scene* scene)
{
    assert(scene != nullptr);
    scene->directional_lights.count = 0;
}

void mel_render_scene_push_directional_light(Mel_Render_Scene* scene,
                                             Mel_Render_Scene_Directional_Light light)
{
    assert(scene != nullptr);
    mel_array_push(&scene->directional_lights, light);
}

const Mel_Render_Scene_Directional_Light* mel_render_scene_directional_lights(
    Mel_Render_Scene* scene, u32* out_count)
{
    assert(scene != nullptr);
    if (out_count) *out_count = (u32)scene->directional_lights.count;
    return scene->directional_lights.items;
}

void mel_render_scene_clear_point_lights(Mel_Render_Scene* scene)
{
    assert(scene != nullptr);
    scene->point_lights.count = 0;
}

void mel_render_scene_push_point_light(Mel_Render_Scene* scene,
                                       Mel_Render_Scene_Point_Light light)
{
    assert(scene != nullptr);
    mel_array_push(&scene->point_lights, light);
}

const Mel_Render_Scene_Point_Light* mel_render_scene_point_lights(
    Mel_Render_Scene* scene, u32* out_count)
{
    assert(scene != nullptr);
    if (out_count) *out_count = (u32)scene->point_lights.count;
    return scene->point_lights.items;
}

Mel_Render_Pipeline_Scene* mel_render_scene_pipeline_scene(Mel_Render_Scene* scene,
                                                           const Mel_Render_Pipeline_Type* type)
{
    assert(scene != nullptr);
    assert(type != nullptr);

    for (usize i = 0; i < scene->pipelines.count; i++)
    {
        Mel_Render_Scene_Pipeline_Entry* entry = &scene->pipelines.items[i];
        if (entry->type != type)
            continue;

        if (entry->last_sync_serial != scene->sync_serial)
        {
            mel_pipeline_scene_sync(entry->scene, &scene->manager);
            entry->last_sync_serial = scene->sync_serial;
        }
        return entry->scene;
    }

    Mel_Render_Pipeline_Scene* pipeline_scene =
        mel_pipeline_scene_create(type, scene, &scene->manager, scene->dev, scene->alloc);

    mel_array_push(&scene->pipelines, ((Mel_Render_Scene_Pipeline_Entry){
        .type = type,
        .scene = pipeline_scene,
        .last_sync_serial = 0,
    }));

    Mel_Render_Scene_Pipeline_Entry* entry = &scene->pipelines.items[scene->pipelines.count - 1];
    if (entry->last_sync_serial != scene->sync_serial)
    {
        mel_pipeline_scene_sync(entry->scene, &scene->manager);
        entry->last_sync_serial = scene->sync_serial;
    }
    return entry->scene;
}
