#include "sponza.scene.h"

void sponza_scene_apply_lighting(Mel_Render_Scene* scene,
                                 Mel_Vec3 world_center,
                                 Mel_Vec3 world_extents)
{
    mel_render_scene_set_environment(scene, (Mel_Render_Scene_Environment){
        .ambient_color = mel_vec4(0.05f, 0.05f, 0.06f, 1.0f),
        .sky_color = mel_vec4(0.22f, 0.24f, 0.28f, 1.0f),
        .ground_color = mel_vec4(0.08f, 0.07f, 0.05f, 1.0f),
        .exposure = 1.0f,
    });
    mel_render_scene_clear_directional_lights(scene);
    mel_render_scene_push_directional_light(scene, (Mel_Render_Scene_Directional_Light){
        .direction_intensity = {{ 0.35f, 1.0f, 0.4f, 1.15f }},
        .color = {{ 1.0f, 0.96f, 0.90f, 1.0f }},
        .shadow_params = {{ 0.0008f, 0.0080f, 1.0f, 0.0f }},
        .flags = MEL_RENDER_SCENE_DIRECTIONAL_LIGHT_CAST_SHADOWS,
    });

    mel_render_scene_clear_point_lights(scene);

    f32 point_range = world_extents.x > world_extents.z ? world_extents.x : world_extents.z;
    point_range *= 0.24f;
    if (point_range < 8.0f)
        point_range = 8.0f;

    f32 point_y = world_center.y + world_extents.y * 0.18f;
    f32 side_x = world_extents.x * 0.34f;
    f32 side_z = world_extents.z * 0.18f;

    mel_render_scene_push_point_light(scene, (Mel_Render_Scene_Point_Light){
        .position_range = {{ world_center.x - side_x, point_y, world_center.z - side_z, point_range }},
        .color_intensity = {{ 1.00f, 0.88f, 0.72f, 1.75f }},
    });
    mel_render_scene_push_point_light(scene, (Mel_Render_Scene_Point_Light){
        .position_range = {{ world_center.x + side_x, point_y, world_center.z - side_z, point_range }},
        .color_intensity = {{ 1.00f, 0.88f, 0.72f, 1.75f }},
    });
    mel_render_scene_push_point_light(scene, (Mel_Render_Scene_Point_Light){
        .position_range = {{ world_center.x - side_x, point_y, world_center.z + side_z, point_range }},
        .color_intensity = {{ 0.74f, 0.82f, 1.00f, 1.25f }},
    });
    mel_render_scene_push_point_light(scene, (Mel_Render_Scene_Point_Light){
        .position_range = {{ world_center.x + side_x, point_y, world_center.z + side_z, point_range }},
        .color_intensity = {{ 0.74f, 0.82f, 1.00f, 1.25f }},
    });
}
