#include "../melody/test.harness.h"
#include "../melody/render.material.h"
#include "../melody/string.str8.h"

MEL_TEST(material_builtin_surface_family_exists, .tags = "render")
{
    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));
    MEL_ASSERT(str8_ieq(mel_material_family_name(surface), S8("surface")));
}

MEL_TEST(material_builtin_sprite_family_exists, .tags = "render")
{
    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));
    MEL_ASSERT(mel_material_family_handle_valid(sprite));
    MEL_ASSERT(str8_ieq(mel_material_family_name(sprite), S8("sprite")));
}

MEL_TEST(material_instance_uses_template_base_color_and_allows_override, .tags = "render")
{
    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));

    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("test_surface_template"),
        .family = surface,
        .profile = S8("surface.unlit"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.25f, 0.50f, 0.75f, 1.0f),
    });

    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);
    Mel_Vec4 base = mel_material_instance_base_color(instance);
    MEL_ASSERT(base.x == 0.25f);
    MEL_ASSERT(base.y == 0.50f);
    MEL_ASSERT(base.z == 0.75f);
    MEL_ASSERT(base.w == 1.0f);

    mel_material_instance_set_base_color(instance, mel_vec4(0.90f, 0.80f, 0.70f, 1.0f));
    Mel_Vec4 overridden = mel_material_instance_base_color(instance);
    MEL_ASSERT(overridden.x == 0.90f);
    MEL_ASSERT(overridden.y == 0.80f);
    MEL_ASSERT(overridden.z == 0.70f);
    MEL_ASSERT(overridden.w == 1.0f);

    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
}

MEL_TEST(material_instance_parameters_fall_back_to_template_and_allow_overrides, .tags = "render")
{
    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));

    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("test_surface_standard_params"),
        .family = surface,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .params = (Mel_Material_Param_Desc[]){
            { .name = S8("roughness"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.35f },
            { .name = S8("metallic"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.10f },
            { .name = S8("emissive"), .type = MEL_MATERIAL_PARAM_VEC4, .vec4_value = { .x = 0.1f, .y = 0.2f, .z = 0.3f, .w = 1.0f } },
        },
        .param_count = 3,
    });

    f32 roughness = 0.0f;
    f32 metallic = 0.0f;
    Mel_Vec4 emissive = {0};
    MEL_ASSERT(mel_material_template_try_get_f32(template, S8("roughness"), &roughness));
    MEL_ASSERT(mel_material_template_try_get_f32(template, S8("metallic"), &metallic));
    MEL_ASSERT(mel_material_template_try_get_vec4(template, S8("emissive"), &emissive));
    MEL_ASSERT_EQ(roughness, 0.35f);
    MEL_ASSERT_EQ(metallic, 0.10f);
    MEL_ASSERT_EQ(emissive.z, 0.3f);

    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);
    MEL_ASSERT(mel_material_instance_try_get_f32(instance, S8("roughness"), &roughness));
    MEL_ASSERT_EQ(roughness, 0.35f);

    u64 version_before = mel_material_instance_parameter_version(instance);
    mel_material_instance_set_f32(instance, S8("roughness"), 0.82f);
    mel_material_instance_set_vec4(instance, S8("emissive"), mel_vec4(0.9f, 0.8f, 0.7f, 1.0f));
    MEL_ASSERT(mel_material_instance_parameter_version(instance) > version_before);
    MEL_ASSERT(mel_material_instance_try_get_f32(instance, S8("roughness"), &roughness));
    MEL_ASSERT_EQ(roughness, 0.82f);
    MEL_ASSERT(mel_material_instance_try_get_vec4(instance, S8("emissive"), &emissive));
    MEL_ASSERT_EQ(emissive.x, 0.9f);
    MEL_ASSERT_EQ(emissive.y, 0.8f);

    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
}

MEL_TEST(material_gpu_record_packs_surface_standard_extended_params, .tags = "render")
{
    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));
    MEL_ASSERT(mel_material_family_handle_valid(surface));

    Mel_Material_Template_Handle template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("test_surface_standard_gpu_record"),
        .family = surface,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(0.6f, 0.7f, 0.8f, 1.0f),
        .params = (Mel_Material_Param_Desc[]){
            { .name = S8("roughness"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.25f },
            { .name = S8("metallic"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.5f },
            { .name = S8("occlusion"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.85f },
            { .name = S8("emissive"), .type = MEL_MATERIAL_PARAM_VEC4, .vec4_value = { .x = 0.2f, .y = 0.3f, .z = 0.4f, .w = 0.7f } },
        },
        .param_count = 4,
    });

    Mel_Material_Instance_Handle instance = mel_material_instance_create(template);
    Mel_Material_Gpu_Record record = mel_material_instance_pack_gpu_record(instance);
    MEL_ASSERT_FLOAT_EQ(record.base_color.x, 0.6f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.params0.x, 0.25f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.params0.y, 0.5f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.params0.z, 1.0f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.params0.w, 0.85f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.params1.x, (f32)MEL_MATERIAL_DOMAIN_OPAQUE, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.emissive_color.x, 0.2f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.emissive_color.y, 0.3f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.emissive_color.z, 0.4f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.emissive_color.w, 0.7f, 0.0001f);

    mel_material_instance_set_f32(instance, S8("occlusion"), 0.42f);
    mel_material_instance_set_vec4(instance, S8("emissive"), mel_vec4(0.8f, 0.1f, 0.6f, 0.5f));
    record = mel_material_instance_pack_gpu_record(instance);
    MEL_ASSERT_FLOAT_EQ(record.params0.w, 0.42f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.emissive_color.x, 0.8f, 0.0001f);
    MEL_ASSERT_FLOAT_EQ(record.emissive_color.w, 0.5f, 0.0001f);

    mel_material_instance_destroy(instance);
    mel_material_template_destroy(template);
}
