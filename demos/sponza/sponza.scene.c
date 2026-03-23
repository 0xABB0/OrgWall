#include "sponza.scene.h"

Mel_Render_Environment_Handle sponza_scene_apply_lighting(Mel_Render_Scene* scene,
                                                          Mel_Vec3 world_center,
                                                          Mel_Vec3 world_extents)
{
    MEL_UNUSED(world_center);
    MEL_UNUSED(world_extents);

    Mel_Render_Environment_Handle environment =
        mel_render_environment_create_constant(mel_vec4(0.08f, 0.10f, 0.13f, 1.0f));
    mel_render_scene_set_environment(scene, environment);
    mel_render_scene_clear_directional_lights(scene);
    mel_render_scene_push_directional_light(scene, (Mel_Render_Scene_Directional_Light){
        .direction_intensity = {{ -0.28f, 0.93f, -0.22f, 1.65f }},
        .color = {{ 1.0f, 0.95f, 0.88f, 1.0f }},
        .shadow_params = {{ 0.0008f, 0.0080f, 0.92f, 0.0f }},
        .flags = MEL_RENDER_SCENE_DIRECTIONAL_LIGHT_CAST_SHADOWS,
    });
    mel_render_scene_clear_point_lights(scene);

    return environment;
}
