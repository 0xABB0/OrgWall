#include "test.harness.h"
#include "render.scene.h"
#include "gpu.device.h"
#include "allocator.heap.h"

MEL_TEST(render_scene_lighting_lists, .tags = "render")
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    MEL_ASSERT_NOT_NULL(dev);

    Mel_Render_Scene* scene = mel_render_scene_create(.dev = dev, .alloc = mel_alloc_heap());
    MEL_ASSERT_NOT_NULL(scene);

    mel_render_scene_set_ambient_color(scene, mel_vec4(0.2f, 0.3f, 0.4f, 1.0f));
    Mel_Vec4 ambient = mel_render_scene_ambient_color(scene);
    MEL_ASSERT_FLOAT_EQ(ambient.x, 0.2f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(ambient.y, 0.3f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(ambient.z, 0.4f, 0.0001f);

    mel_render_scene_clear_directional_lights(scene);
    mel_render_scene_clear_point_lights(scene);

    mel_render_scene_push_directional_light(scene, (Mel_Render_Scene_Directional_Light){
        .direction_intensity = {{ 0.0f, 1.0f, 0.0f, 2.0f }},
        .color = {{ 1.0f, 0.5f, 0.25f, 1.0f }},
        .shadow_params = {{ 0.001f, 0.01f, 0.75f, 0.0f }},
        .flags = MEL_RENDER_SCENE_DIRECTIONAL_LIGHT_CAST_SHADOWS,
    });
    mel_render_scene_push_point_light(scene, (Mel_Render_Scene_Point_Light){
        .position_range = {{ 1.0f, 2.0f, 3.0f, 4.0f }},
        .color_intensity = {{ 0.25f, 0.5f, 1.0f, 3.0f }},
    });

    u32 directional_count = 0;
    u32 point_count = 0;
    const Mel_Render_Scene_Directional_Light* directional =
        mel_render_scene_directional_lights(scene, &directional_count);
    const Mel_Render_Scene_Point_Light* points =
        mel_render_scene_point_lights(scene, &point_count);

    MEL_ASSERT_EQ(directional_count, 1u);
    MEL_ASSERT_EQ(point_count, 1u);
    MEL_ASSERT_NOT_NULL(directional);
    MEL_ASSERT_NOT_NULL(points);
    MEL_ASSERT_FLOAT_EQ(directional[0].direction_intensity.w, 2.0f, 0.0001f);
    MEL_ASSERT_EQ(directional[0].flags, MEL_RENDER_SCENE_DIRECTIONAL_LIGHT_CAST_SHADOWS);
    MEL_ASSERT_FLOAT_EQ(directional[0].shadow_params.x, 0.001f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(directional[0].shadow_params.z, 0.75f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(points[0].position_range.w, 4.0f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(points[0].color_intensity.z, 1.0f, 0.0001f);

    mel_render_scene_destroy(scene);
}
