#pragma once

#include "render.material.fwd.h"
#include "render.frame_plan.fwd.h"
#include "render.view.fwd.h"
#include "render.technique.h"
#include "gpu.device.fwd.h"
#include "gpu.buffer.h"
#include "string.str8.fwd.h"
#include "math.vec4.h"

typedef enum {
    MEL_MATERIAL_DOMAIN_OPAQUE = 0,
    MEL_MATERIAL_DOMAIN_ALPHA_TEST = 1,
    MEL_MATERIAL_DOMAIN_TRANSPARENT = 2,
    MEL_MATERIAL_DOMAIN_OVERLAY = 3,
} Mel_Material_Domain;

typedef enum {
    MEL_MATERIAL_FALLBACK_ALLOW = 0,
    MEL_MATERIAL_FALLBACK_STRICT = 1,
} Mel_Material_Fallback_Policy;

typedef enum {
    MEL_MATERIAL_CHECK_OK = 0,
    MEL_MATERIAL_CHECK_CAPABILITY_UNAVAILABLE = 1,
    MEL_MATERIAL_CHECK_PROFILE_MISMATCH = 2,
    MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH = 3,
    MEL_MATERIAL_CHECK_POLICY_SKIPPED = 4,
    MEL_MATERIAL_CHECK_OTHER = 5,
} Mel_Material_Check_Kind;

typedef enum {
    MEL_MATERIAL_PARAM_NONE = 0,
    MEL_MATERIAL_PARAM_F32 = 1,
    MEL_MATERIAL_PARAM_VEC4 = 2,
} Mel_Material_Param_Type;

typedef struct {
    str8 name;
    u32 type;
    f32 f32_value;
    Mel_Vec4 vec4_value;
} Mel_Material_Param_Desc;

typedef struct {
    bool ok;
    u32 kind;
    str8 reason;
} Mel_Material_Check_Result;

typedef struct {
    Mel_Vec4 base_color;
    Mel_Vec4 params0;
} Mel_Material_Gpu_Record;

struct Mel_Material_Table {
    Mel_Gpu_Device* dev;
    Mel_Gpu_Buffer buffer;
    Mel_Material_Gpu_Record* records;
    u32 count;
    u32 capacity;
};

typedef struct {
    Mel_Gpu_Device* dev;
    u32 capacity;
} Mel_Material_Table_Init_Opt;

typedef struct Mel_Material_Backend_Desc Mel_Material_Backend_Desc;
typedef struct Mel_Material_Family_Policy Mel_Material_Family_Policy;

typedef struct {
    str8 name;
} Mel_Material_Family_Desc;

typedef struct {
    str8 name;
    Mel_Material_Family_Handle family;
    str8 profile;
    u32 render_domain;
    u32 fallback_policy;
    Mel_Vec4 base_color;
    const Mel_Material_Param_Desc* params;
    u32 param_count;
} Mel_Material_Template_Desc;

typedef struct {
    Mel_Material_Template_Handle material;
    bool override_base_color;
    Mel_Vec4 base_color;
    const Mel_Material_Param_Desc* overrides;
    u32 override_count;
} Mel_Material_Instance_Desc;

typedef struct {
    Mel_Frame_Plan_Handle plan;
    Mel_View_Handle view;
    Mel_Gpu_Device* dev;
    Mel_Technique_Family_Id technique_family;
    str8 technique_name;
    u32 binding_index;
} Mel_Frame_Plan_Material_Ctx;

typedef Mel_Material_Check_Result (*Mel_Material_Backend_Check_Fn)(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template);

typedef struct {
    bool allow;
    i32 priority_bias;
    u32 kind;
    str8 reason;
} Mel_Material_Policy_Result;

typedef Mel_Material_Policy_Result (*Mel_Material_Policy_Fn)(Mel_Frame_Plan_Material_Ctx* ctx,
    const Mel_Material_Backend_Desc* backend, Mel_Material_Template_Handle material_template, void* user);

struct Mel_Material_Family_Policy {
    Mel_Material_Family_Handle family;
    Mel_Material_Policy_Fn fn;
    void* user;
};

struct Mel_Material_Backend_Desc {
    str8 name;
    Mel_Material_Family_Handle family;
    str8 profile;
    Mel_Technique_Family_Id technique_family;
    str8 technique_name;
    i32 priority;
    Mel_Material_Backend_Check_Fn supports;
    Mel_Material_Backend_Check_Fn matches;
};

Mel_Material_Family_Handle mel_material_family_register(const Mel_Material_Family_Desc* desc);
Mel_Material_Family_Handle mel_material_family_find(str8 name);
str8 mel_material_family_name(Mel_Material_Family_Handle family);

Mel_Material_Template_Handle mel_material_template_create(const Mel_Material_Template_Desc* desc);
void mel_material_template_destroy(Mel_Material_Template_Handle material);
str8 mel_material_template_name(Mel_Material_Template_Handle material);
Mel_Material_Family_Handle mel_material_template_family(Mel_Material_Template_Handle material);
str8 mel_material_template_profile(Mel_Material_Template_Handle material);
u32 mel_material_template_render_domain(Mel_Material_Template_Handle material);
u32 mel_material_template_fallback_policy(Mel_Material_Template_Handle material);
Mel_Vec4 mel_material_template_base_color(Mel_Material_Template_Handle material);

Mel_Material_Instance_Handle mel_material_instance_create_opt(const Mel_Material_Instance_Desc* desc);
#define mel_material_instance_create(material_handle, ...) \
    mel_material_instance_create_opt(&(Mel_Material_Instance_Desc){ .material = (material_handle), __VA_ARGS__ })
void mel_material_instance_destroy(Mel_Material_Instance_Handle material);
Mel_Material_Template_Handle mel_material_instance_template(Mel_Material_Instance_Handle material);
Mel_Vec4 mel_material_instance_base_color(Mel_Material_Instance_Handle material);
void mel_material_instance_set_base_color(Mel_Material_Instance_Handle material, Mel_Vec4 base_color);
bool mel_material_template_try_get_f32(Mel_Material_Template_Handle material, str8 name, f32* out);
bool mel_material_template_try_get_vec4(Mel_Material_Template_Handle material, str8 name, Mel_Vec4* out);
bool mel_material_instance_try_get_f32(Mel_Material_Instance_Handle material, str8 name, f32* out);
bool mel_material_instance_try_get_vec4(Mel_Material_Instance_Handle material, str8 name, Mel_Vec4* out);
void mel_material_instance_set_f32(Mel_Material_Instance_Handle material, str8 name, f32 value);
void mel_material_instance_set_vec4(Mel_Material_Instance_Handle material, str8 name, Mel_Vec4 value);
u64 mel_material_instance_parameter_version(Mel_Material_Instance_Handle material);
Mel_Material_Gpu_Record mel_material_instance_pack_gpu_record(Mel_Material_Instance_Handle material);

bool mel_material_table_init_opt(Mel_Material_Table* table, Mel_Material_Table_Init_Opt opt);
#define mel_material_table_init(table, ...) mel_material_table_init_opt((table), (Mel_Material_Table_Init_Opt){__VA_ARGS__})
void mel_material_table_shutdown(Mel_Material_Table* table);
void mel_material_table_clear(Mel_Material_Table* table);
u32 mel_material_table_push(Mel_Material_Table* table, Mel_Material_Instance_Handle material);
u32 mel_material_table_push_record(Mel_Material_Table* table, Mel_Material_Gpu_Record record);
void mel_material_table_upload(Mel_Material_Table* table);
Mel_Material_Gpu_Record* mel_material_table_records(Mel_Material_Table* table);
u32 mel_material_table_count(Mel_Material_Table* table);

void mel_material_backend_register(const Mel_Material_Backend_Desc* desc);
void mel_material_backend_unregister(Mel_Material_Family_Handle family, str8 name);
void mel_material_set_family_policy(const Mel_Material_Family_Policy* policy);
void mel_material_clear_family_policy(Mel_Material_Family_Handle family);
u32 mel_material_backend_count_for_family(Mel_Material_Family_Handle family);
const Mel_Material_Backend_Desc* mel_material_backend_at_for_family(Mel_Material_Family_Handle family, u32 index);
Mel_Material_Policy_Result mel_material_eval_family_policy(Mel_Material_Family_Handle family,
    Mel_Frame_Plan_Material_Ctx* ctx, const Mel_Material_Backend_Desc* desc, Mel_Material_Template_Handle material_template);
Mel_Material_Check_Result mel_material_backend_support(const Mel_Material_Backend_Desc* desc,
    Mel_Frame_Plan_Material_Ctx* ctx, Mel_Material_Template_Handle material_template);
Mel_Material_Check_Result mel_material_backend_match(const Mel_Material_Backend_Desc* desc,
    Mel_Frame_Plan_Material_Ctx* ctx, Mel_Material_Template_Handle material_template);
const Mel_Material_Backend_Desc* mel_material_backend_resolve(Mel_Material_Family_Handle family,
    Mel_Frame_Plan_Material_Ctx* ctx, Mel_Material_Template_Handle material_template);
