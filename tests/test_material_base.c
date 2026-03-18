#include "../melody/test.harness.h"
#include "../melody/render.material_base.h"
#include "../melody/math.vec4.h"

typedef struct {
    Mel_Vec4 base_color;
} Unlit_Params;

MEL_TEST(material_base_register, .tags = "render")
{
    Mel_Material_Base_Id id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("test_unlit"),
        .param_size = sizeof(Unlit_Params),
        .compat = MEL_COMPAT_FORWARD,
    });

    MEL_ASSERT_NEQ(id, MEL_MATERIAL_BASE_ID_INVALID);

    Mel_Material_Base_Id found = mel_material_base_find(S8("test_unlit"));
    MEL_ASSERT_EQ(found, id);

    Mel_Material_Base_Id not_found = mel_material_base_find(S8("nonexistent"));
    MEL_ASSERT_EQ(not_found, MEL_MATERIAL_BASE_ID_INVALID);
}

MEL_TEST(material_base_alloc_instance, .tags = "render")
{
    Mel_Material_Base_Id id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("test_alloc"),
        .param_size = sizeof(Unlit_Params),
        .compat = MEL_COMPAT_FORWARD,
    });

    Unlit_Params red = { .base_color = {{ 1.0f, 0.0f, 0.0f, 1.0f }} };
    Unlit_Params blue = { .base_color = {{ 0.0f, 0.0f, 1.0f, 1.0f }} };

    Mel_Material_Instance_Id i0 = mel_material_base_alloc_instance(id, &red);
    Mel_Material_Instance_Id i1 = mel_material_base_alloc_instance(id, &blue);

    MEL_ASSERT_EQ(i0, 0);
    MEL_ASSERT_EQ(i1, 1);

    const Unlit_Params* p0 = mel_material_base_get_params(id, i0);
    MEL_ASSERT_FLOAT_EQ(p0->base_color.r, 1.0f, 0.001f);

    const Unlit_Params* p1 = mel_material_base_get_params(id, i1);
    MEL_ASSERT_FLOAT_EQ(p1->base_color.b, 1.0f, 0.001f);
}

MEL_TEST(material_base_set_params, .tags = "render")
{
    Mel_Material_Base_Id id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("test_set"),
        .param_size = sizeof(Unlit_Params),
        .compat = MEL_COMPAT_FORWARD,
    });

    Unlit_Params initial = { .base_color = {{ 1.0f, 1.0f, 1.0f, 1.0f }} };
    Mel_Material_Instance_Id inst = mel_material_base_alloc_instance(id, &initial);

    Unlit_Params updated = { .base_color = {{ 0.5f, 0.5f, 0.0f, 1.0f }} };
    mel_material_base_set_params(id, inst, &updated);

    const Unlit_Params* p = mel_material_base_get_params(id, inst);
    MEL_ASSERT_FLOAT_EQ(p->base_color.r, 0.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(p->base_color.g, 0.5f, 0.001f);
}

MEL_TEST(material_base_get_struct, .tags = "render")
{
    Mel_Material_Base_Id id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("test_struct"),
        .param_size = sizeof(Unlit_Params),
        .compat = MEL_COMPAT_FORWARD | MEL_COMPAT_DEFERRED,
    });

    Mel_Material_Base* base = mel_material_base_get(id);
    MEL_ASSERT_NOT_NULL(base);
    MEL_ASSERT_EQ(base->param_size, sizeof(Unlit_Params));
    MEL_ASSERT_EQ(base->compat, MEL_COMPAT_FORWARD | MEL_COMPAT_DEFERRED);
}
