#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"

typedef struct Mel_Frame_Plan_Technique_Ctx Mel_Frame_Plan_Technique_Ctx;
typedef struct Mel_Technique_Desc Mel_Technique_Desc;

typedef enum {
    MEL_TECHNIQUE_NONE = 0,
    MEL_TECHNIQUE_SPRITE = 1,
    MEL_TECHNIQUE_TEXT = 2,
    MEL_TECHNIQUE_DEBUG = 3,
    MEL_TECHNIQUE_IMGUI = 4,
    MEL_TECHNIQUE_UI = 5,
    MEL_TECHNIQUE_MESH = 6,
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

struct Mel_Technique_Desc {
    Mel_Technique_Family_Id family;
    str8 name;
    u32 source_schema;
    Mel_Technique_Compile_Fn compile;
};

void mel_render_technique_register(const Mel_Technique_Desc* desc);
const Mel_Technique_Desc* mel_render_technique_get(Mel_Technique_Family_Id family);
str8 mel_render_technique_name(Mel_Technique_Family_Id family);
