#pragma once

#include "core.types.h"
#include "render.frame_plan.fwd.h"
#include "render.graph.fwd.h"
#include "render.view.fwd.h"
#include "string.str8.fwd.h"

typedef struct Mel_Frame_Plan_Technique_Ctx Mel_Frame_Plan_Technique_Ctx;
typedef struct Mel_Technique_Desc Mel_Technique_Desc;
typedef struct Mel_Technique_Family_Policy Mel_Technique_Family_Policy;

typedef enum {
    MEL_TECHNIQUE_NONE = 0,
    MEL_TECHNIQUE_SPRITE = 1,
    MEL_TECHNIQUE_TEXT = 2,
    MEL_TECHNIQUE_DEBUG = 3,
    MEL_TECHNIQUE_IMGUI = 4,
    MEL_TECHNIQUE_UI = 5,
    MEL_TECHNIQUE_MESH = 6,
    MEL_TECHNIQUE_USER_BASE = 1024,
} Mel_Technique_Family_Id;

typedef void (*Mel_ImGui_Draw_Fn)(void* user);

typedef struct {
    Mel_ImGui_Draw_Fn fn;
    void* user;
} Mel_ImGui_Draw_Callback;

typedef enum {
    MEL_TECHNIQUE_COMPILE_FAIL = 0,
    MEL_TECHNIQUE_COMPILE_SKIP = 1,
    MEL_TECHNIQUE_COMPILE_CONTRIBUTED = 2,
} Mel_Technique_Compile_Result;

typedef struct {
    Mel_Frame_Plan_Technique_Ctx* plan_ctx;
    const Mel_Technique_Desc* technique;
} Mel_Technique_Compile_Ctx;

typedef Mel_Technique_Compile_Result (*Mel_Technique_Compile_Fn)(const Mel_Technique_Compile_Ctx* ctx);

typedef enum {
    MEL_TECHNIQUE_CHECK_OK = 0,
    MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE = 1,
    MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH = 2,
    MEL_TECHNIQUE_CHECK_VIEW_MISMATCH = 3,
    MEL_TECHNIQUE_CHECK_POLICY_SKIPPED = 4,
    MEL_TECHNIQUE_CHECK_OTHER = 5,
} Mel_Technique_Check_Kind;

typedef struct {
    bool ok;
    u32 kind;
    str8 reason;
} Mel_Technique_Check_Result;

typedef Mel_Technique_Check_Result (*Mel_Technique_Check_Fn)(Mel_Frame_Plan_Technique_Ctx* ctx);

typedef struct {
    bool allow;
    i32 priority_bias;
    u32 kind;
    str8 reason;
} Mel_Technique_Policy_Result;

typedef Mel_Technique_Policy_Result (*Mel_Technique_Policy_Fn)(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user);

struct Mel_Technique_Family_Policy {
    Mel_Technique_Family_Id family;
    Mel_Technique_Policy_Fn fn;
    void* user;
};

typedef struct {
    Mel_Frame_Plan_Handle plan;
    Mel_Render_Graph* graph;
    Mel_View_Handle view;
    const Mel_Technique_Desc* technique;
    str8* pass_names;
    u32 pass_count;
} Mel_Technique_Refresh_Ctx;

typedef bool (*Mel_Technique_Refresh_Fn)(const Mel_Technique_Refresh_Ctx* ctx);

struct Mel_Technique_Desc {
    Mel_Technique_Family_Id family;
    str8 name;
    u32 source_schema;
    i32 priority;
    Mel_Technique_Check_Fn supports;
    Mel_Technique_Check_Fn matches;
    Mel_Technique_Compile_Fn compile;
    Mel_Technique_Refresh_Fn refresh;
};

void mel_render_technique_register(const Mel_Technique_Desc* desc);
void mel_render_technique_unregister(Mel_Technique_Family_Id family, str8 name);
void mel_render_technique_set_family_policy(const Mel_Technique_Family_Policy* policy);
void mel_render_technique_clear_family_policy(Mel_Technique_Family_Id family);
const Mel_Technique_Desc* mel_render_technique_get(Mel_Technique_Family_Id family);
const Mel_Technique_Desc* mel_render_technique_find(Mel_Technique_Family_Id family, str8 name);
u32 mel_render_technique_count_for_family(Mel_Technique_Family_Id family);
const Mel_Technique_Desc* mel_render_technique_at_for_family(Mel_Technique_Family_Id family, u32 index);
Mel_Technique_Policy_Result mel_render_technique_eval_family_policy(Mel_Technique_Family_Id family,
    Mel_Frame_Plan_Technique_Ctx* ctx, const Mel_Technique_Desc* desc);
Mel_Technique_Check_Result mel_render_technique_support(const Mel_Technique_Desc* desc, Mel_Frame_Plan_Technique_Ctx* ctx);
Mel_Technique_Check_Result mel_render_technique_match(const Mel_Technique_Desc* desc, Mel_Frame_Plan_Technique_Ctx* ctx);
const Mel_Technique_Desc* mel_render_technique_resolve(Mel_Technique_Family_Id family, Mel_Frame_Plan_Technique_Ctx* ctx);
str8 mel_render_technique_name(Mel_Technique_Family_Id family);
