#include "render.material.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"

struct Mel_Material_Family {
    str8 name;
};

struct Mel_Material_Template {
    str8 name;
    Mel_Material_Template_Desc desc;
};

struct Mel_Material_Instance {
    Mel_Material_Template_Handle material;
    bool override_base_color;
    Mel_Vec4 base_color;
    Mel_Material_Param_Desc* overrides;
    u32 override_count;
    u64 parameter_version;
};

typedef struct {
    Mel_Material_Backend_Desc desc;
} Mel__Material_Backend_Entry;

typedef struct {
    Mel_Material_Family_Policy policy;
} Mel__Material_Policy_Entry;

static Mel_SlotMap s_material_families;
static Mel_SlotMap s_material_templates;
static Mel_SlotMap s_material_instances;
static Mel_Array(Mel__Material_Backend_Entry) s_material_backends;
static Mel_Array(Mel__Material_Policy_Entry) s_material_policies;
static bool s_initialized;
static Mel_Material_Family_Handle s_surface_family;
static Mel_Material_Family_Handle s_sprite_family;

static bool mel__material_vec4_equals(Mel_Vec4 a, Mel_Vec4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

static Mel_Material_Param_Desc* mel__material_param_descs_dup(const Mel_Material_Param_Desc* params, u32 count)
{
    if (!params || count == 0)
        return nullptr;

    Mel_Material_Param_Desc* copy = mel_alloc(mel_alloc_heap(), sizeof(*copy) * count);
    for (u32 i = 0; i < count; i++)
    {
        copy[i] = params[i];
        copy[i].name = str8_dup(params[i].name, mel_alloc_heap());
    }
    return copy;
}

static void mel__material_param_descs_free(Mel_Material_Param_Desc* params, u32 count)
{
    if (!params)
        return;

    for (u32 i = 0; i < count; i++)
        mel_dealloc(mel_alloc_heap(), params[i].name.data);
    mel_dealloc(mel_alloc_heap(), params);
}

static Mel_Material_Param_Desc* mel__material_find_param(Mel_Material_Param_Desc* params, u32 count, str8 name)
{
    for (u32 i = 0; i < count; i++)
    {
        if (str8_equals(params[i].name, name))
            return &params[i];
    }
    return nullptr;
}

static const Mel_Material_Param_Desc* mel__material_find_param_const(const Mel_Material_Param_Desc* params, u32 count, str8 name)
{
    for (u32 i = 0; i < count; i++)
    {
        if (str8_equals(params[i].name, name))
            return &params[i];
    }
    return nullptr;
}

static Mel_Material_Param_Desc* mel__material_instance_add_override(Mel_Material_Instance* material, str8 name, u32 type)
{
    u32 new_count = material->override_count + 1;
    Mel_Material_Param_Desc* new_items = mel_alloc(mel_alloc_heap(), sizeof(*new_items) * new_count);

    for (u32 i = 0; i < material->override_count; i++)
        new_items[i] = material->overrides[i];

    new_items[new_count - 1] = (Mel_Material_Param_Desc){
        .name = str8_dup(name, mel_alloc_heap()),
        .type = type,
    };

    mel_dealloc(mel_alloc_heap(), material->overrides);
    material->overrides = new_items;
    material->override_count = new_count;
    return &material->overrides[new_count - 1];
}

static Mel_Material_Check_Result mel__material_support_always(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(material_template);
    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("supported by current device"),
    };
}

static Mel_Material_Policy_Result mel__material_policy_allow_default(Mel_Frame_Plan_Material_Ctx* ctx,
    const Mel_Material_Backend_Desc* backend, Mel_Material_Template_Handle material_template, void* user)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(backend);
    MEL_UNUSED(material_template);
    MEL_UNUSED(user);
    return (Mel_Material_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8(""),
    };
}

static Mel_Material_Check_Result mel__surface_unlit_forward_match(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    if (!str8_ieq(ctx->technique_name, S8("mesh.forward")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH,
            .reason = S8("surface.unlit.forward requires mesh.forward"),
        };
    }

    if (!str8_ieq(mel_material_template_profile(material_template), S8("surface.unlit")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_PROFILE_MISMATCH,
            .reason = S8("backend expects surface.unlit profile"),
        };
    }

    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("surface.unlit material matches mesh.forward"),
    };
}

static Mel_Material_Check_Result mel__sprite_unlit_match(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    if (!str8_ieq(ctx->technique_name, S8("sprite")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH,
            .reason = S8("sprite.unlit.sprite requires sprite technique"),
        };
    }

    if (!str8_ieq(mel_material_template_profile(material_template), S8("sprite.unlit")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_PROFILE_MISMATCH,
            .reason = S8("backend expects sprite.unlit profile"),
        };
    }

    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("sprite.unlit material matches sprite technique"),
    };
}

static Mel_Material_Check_Result mel__surface_standard_forward_match(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    if (!str8_ieq(ctx->technique_name, S8("mesh.forward")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH,
            .reason = S8("surface.standard.forward requires mesh.forward"),
        };
    }

    if (!str8_ieq(mel_material_template_profile(material_template), S8("surface.standard")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_PROFILE_MISMATCH,
            .reason = S8("backend expects surface.standard profile"),
        };
    }

    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("surface.standard material matches mesh.forward"),
    };
}

static Mel_Material_Family* mel__material_family_get(Mel_Material_Family_Handle handle)
{
    assert(s_initialized);
    Mel_Material_Family* family = mel_slotmap_get(&s_material_families, handle.handle);
    assert(family != nullptr);
    return family;
}

static Mel_Material_Template* mel__material_template_get(Mel_Material_Template_Handle handle)
{
    assert(s_initialized);
    Mel_Material_Template* material = mel_slotmap_get(&s_material_templates, handle.handle);
    assert(material != nullptr);
    return material;
}

static Mel_Material_Instance* mel__material_instance_get(Mel_Material_Instance_Handle handle)
{
    assert(s_initialized);
    Mel_Material_Instance* material = mel_slotmap_get(&s_material_instances, handle.handle);
    assert(material != nullptr);
    return material;
}

__attribute__((constructor(215)))
static void mel__material_registry_init(void)
{
    mel_slotmap_init(&s_material_families, mel_alloc_heap(),
        .item_size = sizeof(Mel_Material_Family), .initial_capacity = 8);
    mel_slotmap_init(&s_material_templates, mel_alloc_heap(),
        .item_size = sizeof(Mel_Material_Template), .initial_capacity = 32);
    mel_slotmap_init(&s_material_instances, mel_alloc_heap(),
        .item_size = sizeof(Mel_Material_Instance), .initial_capacity = 64);
    mel_array_init(&s_material_backends, mel_alloc_heap());
    mel_array_init(&s_material_policies, mel_alloc_heap());
    s_initialized = true;

    s_surface_family = mel_material_family_register(&(Mel_Material_Family_Desc){
        .name = S8("surface"),
    });
    s_sprite_family = mel_material_family_register(&(Mel_Material_Family_Desc){
        .name = S8("sprite"),
    });

    mel_material_backend_register(&(Mel_Material_Backend_Desc){
        .name = S8("surface.unlit.forward"),
        .family = s_surface_family,
        .profile = S8("surface.unlit"),
        .technique_family = MEL_TECHNIQUE_MESH,
        .technique_name = S8("mesh.forward"),
        .priority = 100,
        .supports = mel__material_support_always,
        .matches = mel__surface_unlit_forward_match,
    });
    mel_material_backend_register(&(Mel_Material_Backend_Desc){
        .name = S8("surface.standard.forward"),
        .family = s_surface_family,
        .profile = S8("surface.standard"),
        .technique_family = MEL_TECHNIQUE_MESH,
        .technique_name = S8("mesh.forward"),
        .priority = 100,
        .supports = mel__material_support_always,
        .matches = mel__surface_standard_forward_match,
    });
    mel_material_backend_register(&(Mel_Material_Backend_Desc){
        .name = S8("sprite.unlit.sprite"),
        .family = s_sprite_family,
        .profile = S8("sprite.unlit"),
        .technique_family = MEL_TECHNIQUE_SPRITE,
        .technique_name = S8("sprite"),
        .priority = 100,
        .supports = mel__material_support_always,
        .matches = mel__sprite_unlit_match,
    });
}

__attribute__((destructor(215)))
static void mel__material_registry_shutdown(void)
{
    if (!s_initialized)
        return;

    Mel_Material_Family* families = mel_slotmap_data(&s_material_families);
    u32 family_count = mel_slotmap_count(&s_material_families);
    for (u32 i = 0; i < family_count; i++)
        mel_dealloc(mel_alloc_heap(), families[i].name.data);

    Mel_Material_Template* materials = mel_slotmap_data(&s_material_templates);
    u32 material_count = mel_slotmap_count(&s_material_templates);
    for (u32 i = 0; i < material_count; i++)
    {
        mel_dealloc(mel_alloc_heap(), materials[i].name.data);
        mel_dealloc(mel_alloc_heap(), materials[i].desc.name.data);
        mel_dealloc(mel_alloc_heap(), materials[i].desc.profile.data);
        mel__material_param_descs_free((Mel_Material_Param_Desc*)materials[i].desc.params, materials[i].desc.param_count);
    }

    Mel_Material_Instance* instances = mel_slotmap_data(&s_material_instances);
    u32 instance_count = mel_slotmap_count(&s_material_instances);
    for (u32 i = 0; i < instance_count; i++)
        mel__material_param_descs_free(instances[i].overrides, instances[i].override_count);

    for (usize i = 0; i < s_material_backends.count; i++)
    {
        mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.name.data);
        mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.profile.data);
        mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.technique_name.data);
    }

    mel_array_free(&s_material_backends);
    mel_array_free(&s_material_policies);
    mel_slotmap_free(&s_material_instances);
    mel_slotmap_free(&s_material_templates);
    mel_slotmap_free(&s_material_families);
    s_initialized = false;
}

Mel_Material_Family_Handle mel_material_family_register(const Mel_Material_Family_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);

    Mel_Material_Family_Handle existing = mel_material_family_find(desc->name);
    if (mel_material_family_handle_valid(existing))
        return existing;

    Mel_Material_Family family = {
        .name = str8_dup(desc->name, mel_alloc_heap()),
    };
    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_material_families, &family);
    return (Mel_Material_Family_Handle){ .handle = raw };
}

Mel_Material_Family_Handle mel_material_family_find(str8 name)
{
    assert(s_initialized);
    Mel_Material_Family* families = mel_slotmap_data(&s_material_families);
    u32 count = mel_slotmap_count(&s_material_families);
    for (u32 i = 0; i < count; i++)
    {
        Mel_SlotMap_Slot* slots = s_material_families.slots;
        if (str8_ieq(families[i].name, name))
        {
            u32 slot_index = s_material_families.packed_to_slot[i];
            return (Mel_Material_Family_Handle){ .handle = mel_slotmap_handle_make(slot_index, slots[slot_index].generation) };
        }
    }
    return MEL_MATERIAL_FAMILY_HANDLE_NULL;
}

str8 mel_material_family_name(Mel_Material_Family_Handle handle)
{
    return mel__material_family_get(handle)->name;
}

Mel_Material_Template_Handle mel_material_template_create(const Mel_Material_Template_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);
    assert(mel_material_family_handle_valid(desc->family));

    Mel_Material_Template material = {
        .name = str8_dup(desc->name, mel_alloc_heap()),
        .desc = *desc,
    };
    material.desc.name = str8_dup(desc->name, mel_alloc_heap());
    material.desc.profile = str8_dup(desc->profile, mel_alloc_heap());
    material.desc.params = mel__material_param_descs_dup(desc->params, desc->param_count);
    material.desc.param_count = desc->param_count;

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_material_templates, &material);
    return (Mel_Material_Template_Handle){ .handle = raw };
}

void mel_material_template_destroy(Mel_Material_Template_Handle handle)
{
    assert(s_initialized);
    Mel_Material_Template* material = mel__material_template_get(handle);
    mel_dealloc(mel_alloc_heap(), material->name.data);
    mel_dealloc(mel_alloc_heap(), material->desc.name.data);
    mel_dealloc(mel_alloc_heap(), material->desc.profile.data);
    mel__material_param_descs_free((Mel_Material_Param_Desc*)material->desc.params, material->desc.param_count);
    mel_slotmap_remove(&s_material_templates, handle.handle);
}

str8 mel_material_template_name(Mel_Material_Template_Handle handle)
{
    return mel__material_template_get(handle)->desc.name;
}

Mel_Material_Family_Handle mel_material_template_family(Mel_Material_Template_Handle handle)
{
    return mel__material_template_get(handle)->desc.family;
}

str8 mel_material_template_profile(Mel_Material_Template_Handle handle)
{
    return mel__material_template_get(handle)->desc.profile;
}

u32 mel_material_template_render_domain(Mel_Material_Template_Handle handle)
{
    return mel__material_template_get(handle)->desc.render_domain;
}

u32 mel_material_template_fallback_policy(Mel_Material_Template_Handle handle)
{
    return mel__material_template_get(handle)->desc.fallback_policy;
}

Mel_Vec4 mel_material_template_base_color(Mel_Material_Template_Handle handle)
{
    return mel__material_template_get(handle)->desc.base_color;
}

Mel_Material_Instance_Handle mel_material_instance_create_opt(const Mel_Material_Instance_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);
    assert(mel_material_template_handle_valid(desc->material));

    Mel_Material_Instance material = {
        .material = desc->material,
        .override_base_color = desc->override_base_color,
        .base_color = desc->base_color,
        .overrides = mel__material_param_descs_dup(desc->overrides, desc->override_count),
        .override_count = desc->override_count,
        .parameter_version = 1,
    };
    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_material_instances, &material);
    return (Mel_Material_Instance_Handle){ .handle = raw };
}

void mel_material_instance_destroy(Mel_Material_Instance_Handle handle)
{
    assert(s_initialized);
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    mel__material_param_descs_free(material->overrides, material->override_count);
    mel_slotmap_remove(&s_material_instances, handle.handle);
}

Mel_Material_Template_Handle mel_material_instance_template(Mel_Material_Instance_Handle handle)
{
    return mel__material_instance_get(handle)->material;
}

Mel_Vec4 mel_material_instance_base_color(Mel_Material_Instance_Handle handle)
{
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    return material->override_base_color
        ? material->base_color
        : mel_material_template_base_color(material->material);
}

void mel_material_instance_set_base_color(Mel_Material_Instance_Handle handle, Mel_Vec4 base_color)
{
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    if (material->override_base_color && mel__material_vec4_equals(material->base_color, base_color))
        return;
    material->override_base_color = true;
    material->base_color = base_color;
    material->parameter_version++;
}

bool mel_material_template_try_get_f32(Mel_Material_Template_Handle handle, str8 name, f32* out)
{
    Mel_Material_Template* material = mel__material_template_get(handle);
    const Mel_Material_Param_Desc* param = mel__material_find_param_const(material->desc.params, material->desc.param_count, name);
    if (!param || param->type != MEL_MATERIAL_PARAM_F32)
        return false;
    if (out)
        *out = param->f32_value;
    return true;
}

bool mel_material_template_try_get_vec4(Mel_Material_Template_Handle handle, str8 name, Mel_Vec4* out)
{
    Mel_Material_Template* material = mel__material_template_get(handle);
    const Mel_Material_Param_Desc* param = mel__material_find_param_const(material->desc.params, material->desc.param_count, name);
    if (!param || param->type != MEL_MATERIAL_PARAM_VEC4)
        return false;
    if (out)
        *out = param->vec4_value;
    return true;
}

bool mel_material_instance_try_get_f32(Mel_Material_Instance_Handle handle, str8 name, f32* out)
{
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    const Mel_Material_Param_Desc* override = mel__material_find_param_const(material->overrides, material->override_count, name);
    if (override)
    {
        if (override->type != MEL_MATERIAL_PARAM_F32)
            return false;
        if (out)
            *out = override->f32_value;
        return true;
    }
    return mel_material_template_try_get_f32(material->material, name, out);
}

bool mel_material_instance_try_get_vec4(Mel_Material_Instance_Handle handle, str8 name, Mel_Vec4* out)
{
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    const Mel_Material_Param_Desc* override = mel__material_find_param_const(material->overrides, material->override_count, name);
    if (override)
    {
        if (override->type != MEL_MATERIAL_PARAM_VEC4)
            return false;
        if (out)
            *out = override->vec4_value;
        return true;
    }
    return mel_material_template_try_get_vec4(material->material, name, out);
}

void mel_material_instance_set_f32(Mel_Material_Instance_Handle handle, str8 name, f32 value)
{
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    Mel_Material_Param_Desc* override = mel__material_find_param(material->overrides, material->override_count, name);
    if (!override)
        override = mel__material_instance_add_override(material, name, MEL_MATERIAL_PARAM_F32);

    if (override->type == MEL_MATERIAL_PARAM_F32 && override->f32_value == value)
        return;

    override->type = MEL_MATERIAL_PARAM_F32;
    override->f32_value = value;
    override->vec4_value = mel_vec4(0.0f, 0.0f, 0.0f, 0.0f);
    material->parameter_version++;
}

void mel_material_instance_set_vec4(Mel_Material_Instance_Handle handle, str8 name, Mel_Vec4 value)
{
    Mel_Material_Instance* material = mel__material_instance_get(handle);
    Mel_Material_Param_Desc* override = mel__material_find_param(material->overrides, material->override_count, name);
    if (!override)
        override = mel__material_instance_add_override(material, name, MEL_MATERIAL_PARAM_VEC4);

    if (override->type == MEL_MATERIAL_PARAM_VEC4 && mel__material_vec4_equals(override->vec4_value, value))
        return;

    override->type = MEL_MATERIAL_PARAM_VEC4;
    override->vec4_value = value;
    override->f32_value = 0.0f;
    material->parameter_version++;
}

u64 mel_material_instance_parameter_version(Mel_Material_Instance_Handle handle)
{
    return mel__material_instance_get(handle)->parameter_version;
}

Mel_Material_Gpu_Record mel_material_instance_pack_gpu_record(Mel_Material_Instance_Handle handle)
{
    Mel_Material_Gpu_Record record = {
        .base_color = mel_material_instance_base_color(handle),
        .params0 = mel_vec4(0.45f, 0.0f, 0.0f, 0.0f),
    };

    Mel_Material_Template_Handle material_template = mel_material_instance_template(handle);
    if (str8_ieq(mel_material_template_profile(material_template), S8("surface.standard")))
    {
        mel_material_instance_try_get_f32(handle, S8("roughness"), &record.params0.x);
        mel_material_instance_try_get_f32(handle, S8("metallic"), &record.params0.y);
        record.params0.z = 1.0f;
    }
    else if (str8_ieq(mel_material_template_profile(material_template), S8("surface.unlit")))
    {
        record.params0.z = 0.0f;
    }
    else if (str8_ieq(mel_material_template_profile(material_template), S8("sprite.unlit")))
    {
        record.params0.z = 0.0f;
    }

    return record;
}

bool mel_material_table_init_opt(Mel_Material_Table* table, Mel_Material_Table_Init_Opt opt)
{
    assert(table != nullptr);
    assert(opt.dev != nullptr);

    *table = (Mel_Material_Table){0};
    table->dev = opt.dev;
    table->capacity = opt.capacity > 0 ? opt.capacity : 256;
    mel_gpu_buffer_init(&table->buffer, opt.dev,
        .size = sizeof(Mel_Material_Gpu_Record) * table->capacity,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .map_on_create = true);
    table->records = table->buffer.mapped;
    return true;
}

void mel_material_table_shutdown(Mel_Material_Table* table)
{
    assert(table != nullptr);
    if (!table->dev)
        return;
    mel_gpu_buffer_shutdown(&table->buffer, table->dev);
    *table = (Mel_Material_Table){0};
}

void mel_material_table_clear(Mel_Material_Table* table)
{
    assert(table != nullptr);
    table->count = 0;
}

u32 mel_material_table_push(Mel_Material_Table* table, Mel_Material_Instance_Handle material)
{
    assert(table != nullptr);
    assert(table->records != nullptr);
    assert(table->count < table->capacity);
    u32 index = table->count++;
    table->records[index] = mel_material_instance_pack_gpu_record(material);
    return index;
}

u32 mel_material_table_push_record(Mel_Material_Table* table, Mel_Material_Gpu_Record record)
{
    assert(table != nullptr);
    assert(table->records != nullptr);
    assert(table->count < table->capacity);
    u32 index = table->count++;
    table->records[index] = record;
    return index;
}

void mel_material_table_upload(Mel_Material_Table* table)
{
    assert(table != nullptr);
    if (!table->dev)
        return;
    mel_gpu_buffer_flush(&table->buffer, table->dev);
}

Mel_Material_Gpu_Record* mel_material_table_records(Mel_Material_Table* table)
{
    assert(table != nullptr);
    return table->records;
}

u32 mel_material_table_count(Mel_Material_Table* table)
{
    assert(table != nullptr);
    return table->count;
}

void mel_material_backend_register(const Mel_Material_Backend_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);
    assert(mel_material_family_handle_valid(desc->family));

    for (usize i = 0; i < s_material_backends.count; i++)
    {
        if (s_material_backends.items[i].desc.family.handle.index == desc->family.handle.index &&
            s_material_backends.items[i].desc.family.handle.generation == desc->family.handle.generation &&
            str8_equals(s_material_backends.items[i].desc.name, desc->name))
        {
            mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.name.data);
            mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.profile.data);
            mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.technique_name.data);
            s_material_backends.items[i].desc = *desc;
            s_material_backends.items[i].desc.name = str8_dup(desc->name, mel_alloc_heap());
            s_material_backends.items[i].desc.profile = str8_dup(desc->profile, mel_alloc_heap());
            s_material_backends.items[i].desc.technique_name = str8_dup(desc->technique_name, mel_alloc_heap());
            return;
        }
    }

    Mel__Material_Backend_Entry entry = {
        .desc = *desc,
    };
    entry.desc.name = str8_dup(desc->name, mel_alloc_heap());
    entry.desc.profile = str8_dup(desc->profile, mel_alloc_heap());
    entry.desc.technique_name = str8_dup(desc->technique_name, mel_alloc_heap());
    mel_array_push(&s_material_backends, entry);
}

void mel_material_backend_unregister(Mel_Material_Family_Handle family, str8 name)
{
    assert(s_initialized);
    for (usize i = 0; i < s_material_backends.count; i++)
    {
        if (s_material_backends.items[i].desc.family.handle.index == family.handle.index &&
            s_material_backends.items[i].desc.family.handle.generation == family.handle.generation &&
            str8_equals(s_material_backends.items[i].desc.name, name))
        {
            mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.name.data);
            mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.profile.data);
            mel_dealloc(mel_alloc_heap(), s_material_backends.items[i].desc.technique_name.data);
            mel_array_remove_ordered(&s_material_backends, i);
            return;
        }
    }
}

void mel_material_set_family_policy(const Mel_Material_Family_Policy* policy)
{
    assert(s_initialized);
    assert(policy != nullptr);
    assert(mel_material_family_handle_valid(policy->family));
    assert(policy->fn != nullptr);

    for (usize i = 0; i < s_material_policies.count; i++)
    {
        if (s_material_policies.items[i].policy.family.handle.index != policy->family.handle.index ||
            s_material_policies.items[i].policy.family.handle.generation != policy->family.handle.generation)
            continue;

        s_material_policies.items[i].policy = *policy;
        return;
    }

    mel_array_push(&s_material_policies, ((Mel__Material_Policy_Entry){ .policy = *policy }));
}

void mel_material_clear_family_policy(Mel_Material_Family_Handle family)
{
    assert(s_initialized);
    for (usize i = 0; i < s_material_policies.count; i++)
    {
        if (s_material_policies.items[i].policy.family.handle.index != family.handle.index ||
            s_material_policies.items[i].policy.family.handle.generation != family.handle.generation)
            continue;

        mel_array_remove_ordered(&s_material_policies, i);
        return;
    }
}

u32 mel_material_backend_count_for_family(Mel_Material_Family_Handle family)
{
    assert(s_initialized);
    u32 count = 0;
    for (usize i = 0; i < s_material_backends.count; i++)
    {
        if (s_material_backends.items[i].desc.family.handle.index == family.handle.index &&
            s_material_backends.items[i].desc.family.handle.generation == family.handle.generation)
            count++;
    }
    return count;
}

const Mel_Material_Backend_Desc* mel_material_backend_at_for_family(Mel_Material_Family_Handle family, u32 index)
{
    assert(s_initialized);
    u32 count = 0;
    for (usize i = 0; i < s_material_backends.count; i++)
    {
        if (s_material_backends.items[i].desc.family.handle.index != family.handle.index ||
            s_material_backends.items[i].desc.family.handle.generation != family.handle.generation)
            continue;
        if (count == index)
            return &s_material_backends.items[i].desc;
        count++;
    }
    return nullptr;
}

Mel_Material_Policy_Result mel_material_eval_family_policy(Mel_Material_Family_Handle family,
    Mel_Frame_Plan_Material_Ctx* ctx, const Mel_Material_Backend_Desc* desc,
    Mel_Material_Template_Handle material_template)
{
    assert(s_initialized);
    for (usize i = 0; i < s_material_policies.count; i++)
    {
        if (s_material_policies.items[i].policy.family.handle.index != family.handle.index ||
            s_material_policies.items[i].policy.family.handle.generation != family.handle.generation)
            continue;

        return s_material_policies.items[i].policy.fn(ctx, desc, material_template,
            s_material_policies.items[i].policy.user);
    }

    return mel__material_policy_allow_default(ctx, desc, material_template, nullptr);
}

Mel_Material_Check_Result mel_material_backend_support(const Mel_Material_Backend_Desc* desc,
    Mel_Frame_Plan_Material_Ctx* ctx, Mel_Material_Template_Handle material_template)
{
    assert(desc != nullptr);
    if (!desc->supports)
        return (Mel_Material_Check_Result){ .ok = true, .kind = MEL_MATERIAL_CHECK_OK, .reason = S8("no support filter") };
    return desc->supports(ctx, material_template);
}

Mel_Material_Check_Result mel_material_backend_match(const Mel_Material_Backend_Desc* desc,
    Mel_Frame_Plan_Material_Ctx* ctx, Mel_Material_Template_Handle material_template)
{
    assert(desc != nullptr);
    if (!desc->matches)
        return (Mel_Material_Check_Result){ .ok = true, .kind = MEL_MATERIAL_CHECK_OK, .reason = S8("no match filter") };
    return desc->matches(ctx, material_template);
}

const Mel_Material_Backend_Desc* mel_material_backend_resolve(Mel_Material_Family_Handle family,
    Mel_Frame_Plan_Material_Ctx* ctx, Mel_Material_Template_Handle material_template)
{
    assert(s_initialized);
    const Mel_Material_Backend_Desc* best = nullptr;
    i32 best_priority = 0;
    u32 count = mel_material_backend_count_for_family(family);
    for (u32 i = 0; i < count; i++)
    {
        const Mel_Material_Backend_Desc* desc = mel_material_backend_at_for_family(family, i);
        if (!desc)
            continue;

        Mel_Material_Check_Result support = mel_material_backend_support(desc, ctx, material_template);
        if (!support.ok)
            continue;
        Mel_Material_Check_Result match = mel_material_backend_match(desc, ctx, material_template);
        if (!match.ok)
            continue;
        Mel_Material_Policy_Result policy = mel_material_eval_family_policy(family, ctx, desc, material_template);
        if (!policy.allow)
            continue;

        i32 effective_priority = desc->priority + policy.priority_bias;
        if (!best || effective_priority > best_priority)
        {
            best = desc;
            best_priority = effective_priority;
        }
    }
    return best;
}
