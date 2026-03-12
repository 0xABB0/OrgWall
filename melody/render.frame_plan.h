#pragma once

#include "render.frame_plan.fwd.h"
#include "render.frame_recipe.h"
#include "render.graph.fwd.h"
#include "render.target.fwd.h"
#include "render.view.fwd.h"
#include "render.pass.h"
#include "render.source.h"
#include "render.technique.h"
#include "render.material.h"
#include "gpu.device.fwd.h"
#include "mesh.pass.fwd.h"
#include "sprite.pass.fwd.h"
#include "text.pass.fwd.h"
#include "swapchain.fwd.h"
#include "string.str8.fwd.h"

typedef struct {
    Mel_Render_Graph* graph;
    Mel_Gpu_Device* dev;
    Mel_Mesh_Pass* mesh_pass;
    Mel_Sprite_Pass* sprite_pass;
    Mel_Text_Pass* text_pass;
} Mel_Frame_Plan_Compile_Opt;

typedef struct {
    Mel_Gpu_Device* dev;
} Mel_Frame_Plan_Refresh_Opt;

typedef enum {
    MEL_FRAME_DIRTY_NONE = 0,
    MEL_FRAME_DIRTY_TOPOLOGY = 1 << 0,
    MEL_FRAME_DIRTY_PARAMETERS = 1 << 1,
    MEL_FRAME_DIRTY_SOURCE_SHAPE = 1 << 2,
} Mel_Frame_Dirty_Flags;

typedef struct {
    Mel_View_Handle view;
    Mel_Technique_Family_Id family;
    str8 technique_name;
    u32 binding_index;
} Mel_Frame_Plan_Resolved_Technique;

typedef struct {
    Mel_View_Handle view;
    Mel_Material_Template_Handle material_template;
    Mel_Material_Instance_Handle material_instance;
    str8 technique_name;
    str8 backend_name;
    u32 binding_index;
} Mel_Frame_Plan_Resolved_Material;

typedef struct {
    Mel_View_Handle view;
    Mel_Technique_Family_Id family;
    str8 technique_name;
    str8 reason;
    u32 binding_index;
    u32 reason_kind;
    bool supported;
    bool matched;
    bool selected;
} Mel_Frame_Plan_Technique_Diagnostic;

typedef struct {
    Mel_View_Handle view;
    Mel_Material_Template_Handle material_template;
    Mel_Material_Instance_Handle material_instance;
    str8 technique_name;
    str8 backend_name;
    str8 reason;
    u32 binding_index;
    u32 reason_kind;
    bool supported;
    bool matched;
    bool selected;
} Mel_Frame_Plan_Material_Diagnostic;

struct Mel_Frame_Plan_Technique_Ctx {
    Mel_Frame_Plan_Handle plan;
    Mel_Frame_Recipe_Handle recipe;
    Mel_Frame_Recipe_Binding_Desc binding;
    u32 binding_index;
    str8 recipe_name;
    Mel_Render_Graph* graph;
    Mel_Gpu_Device* dev;
    Mel_Render_Target* target;
    Mel_Frame_Plan_Compile_Opt opt;
    bool first_for_swapchain;
    bool replace_contents;
    bool* wrote_any_pass;
};

Mel_Frame_Plan_Handle mel_frame_plan_create(str8 name);
void mel_frame_plan_destroy(Mel_Frame_Plan_Handle plan);

bool mel_frame_plan_compile_opt(Mel_Frame_Plan_Handle plan, Mel_Frame_Recipe_Handle recipe, Mel_Frame_Plan_Compile_Opt opt);
#define mel_frame_plan_compile(plan, recipe, ...) mel_frame_plan_compile_opt((plan), (recipe), (Mel_Frame_Plan_Compile_Opt){__VA_ARGS__})
bool mel_frame_plan_refresh_opt(Mel_Frame_Plan_Handle plan, Mel_Frame_Plan_Refresh_Opt opt);
#define mel_frame_plan_refresh(plan, ...) mel_frame_plan_refresh_opt((plan), (Mel_Frame_Plan_Refresh_Opt){__VA_ARGS__})
u32 mel_frame_plan_dirty_flags(Mel_Frame_Plan_Handle plan);

Mel_Render_Target* mel_frame_plan_swapchain_target(Mel_Frame_Plan_Handle plan, Mel_Swapchain_Handle swapchain);
Mel_Render_Target* mel_frame_plan_swapchain_depth_target(Mel_Frame_Plan_Handle plan, Mel_Swapchain_Handle swapchain);
u32 mel_frame_plan_resolved_technique_count(Mel_Frame_Plan_Handle plan);
bool mel_frame_plan_resolved_technique_at(Mel_Frame_Plan_Handle plan, u32 index, Mel_Frame_Plan_Resolved_Technique* out);
u32 mel_frame_plan_resolved_material_count(Mel_Frame_Plan_Handle plan);
bool mel_frame_plan_resolved_material_at(Mel_Frame_Plan_Handle plan, u32 index, Mel_Frame_Plan_Resolved_Material* out);
u32 mel_frame_plan_technique_diagnostic_count(Mel_Frame_Plan_Handle plan);
bool mel_frame_plan_technique_diagnostic_at(Mel_Frame_Plan_Handle plan, u32 index, Mel_Frame_Plan_Technique_Diagnostic* out);
u32 mel_frame_plan_material_diagnostic_count(Mel_Frame_Plan_Handle plan);
bool mel_frame_plan_material_diagnostic_at(Mel_Frame_Plan_Handle plan, u32 index, Mel_Frame_Plan_Material_Diagnostic* out);

Mel_Render_List** mel_frame_plan_collect_render_lists(Mel_Frame_Plan_Handle plan, Mel_View_Handle view, u32 schema);
Mel_Source_Handle* mel_frame_plan_collect_sources(Mel_Frame_Plan_Handle plan, Mel_View_Handle view, Mel_Source_Kind kind, u32 schema);
void mel_frame_plan_free_read_lists(Mel_Frame_Plan_Handle plan, Mel_Render_List** lists);
void mel_frame_plan_free_read_sources(Mel_Frame_Plan_Handle plan, Mel_Source_Handle* sources);
bool mel_frame_plan_add_graphics_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists, Mel_Source_Handle* read_sources,
    Mel_Render_Target** read_targets, Mel_Pass_Write_Target* write_targets);
bool mel_frame_plan_add_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists, Mel_Source_Handle* read_sources, Mel_Render_Target** read_targets);
bool mel_frame_plan_add_render_list_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists);
bool mel_frame_plan_refresh_contributed_passes(Mel_Frame_Plan_Handle plan, Mel_View_Handle view,
    str8* pass_names, u32 pass_count);
