#pragma once

#include "render.frame_recipe.fwd.h"
#include "render.technique.h"
#include "render.view.fwd.h"
#include "render.graph.fwd.h"
#include "swapchain.fwd.h"
#include "allocator.fwd.h"
#include "gpu.device.fwd.h"
#include "sprite.pass.fwd.h"
#include "string.str8.fwd.h"

typedef struct {
    Mel_Render_Graph* graph;
    Mel_Gpu_Device* dev;
    Mel_Sprite_Pass* sprite_pass;
    const Mel_Alloc* alloc;
} Mel_Frame_Recipe_Compile_Opt;

Mel_Frame_Recipe_Handle mel_frame_recipe_create(str8 name);
void mel_frame_recipe_destroy(Mel_Frame_Recipe_Handle recipe);

void mel_frame_recipe_use_technique(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Technique_Family_Id family);
void mel_frame_recipe_disable_technique(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Technique_Family_Id family);

void mel_frame_recipe_present(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Swapchain_Handle swapchain);
void mel_frame_recipe_overlay(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Swapchain_Handle swapchain);

bool mel_frame_recipe_compile_opt(Mel_Frame_Recipe_Handle recipe, Mel_Frame_Recipe_Compile_Opt opt);
#define mel_frame_recipe_compile(recipe, ...) mel_frame_recipe_compile_opt((recipe), (Mel_Frame_Recipe_Compile_Opt){__VA_ARGS__})
