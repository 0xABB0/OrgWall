#pragma once

#include "core.types.h"
#include "render.frame_recipe.fwd.h"
#include "render.technique.h"
#include "render.view.fwd.h"
#include "swapchain.fwd.h"
#include "string.str8.fwd.h"

typedef struct {
    Mel_View_Handle view;
    Mel_Swapchain_Handle swapchain;
    bool overlay;
    i32 order;
} Mel_Frame_Recipe_Binding_Desc;

Mel_Frame_Recipe_Handle mel_frame_recipe_create(str8 name);
void mel_frame_recipe_destroy(Mel_Frame_Recipe_Handle recipe);

void mel_frame_recipe_use_technique(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Technique_Family_Id family);
void mel_frame_recipe_disable_technique(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Technique_Family_Id family);

void mel_frame_recipe_present(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Swapchain_Handle swapchain);
void mel_frame_recipe_overlay(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Swapchain_Handle swapchain);
void mel_frame_recipe_present_ordered(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Swapchain_Handle swapchain, i32 order);
void mel_frame_recipe_overlay_ordered(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Swapchain_Handle swapchain, i32 order);

str8 mel_frame_recipe_name(Mel_Frame_Recipe_Handle recipe);
bool mel_frame_recipe_uses_technique(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, Mel_Technique_Family_Id family);
u32 mel_frame_recipe_technique_count_for_view(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view);
bool mel_frame_recipe_technique_at_for_view(Mel_Frame_Recipe_Handle recipe, Mel_View_Handle view, u32 index, Mel_Technique_Family_Id* out);
u32 mel_frame_recipe_binding_count(Mel_Frame_Recipe_Handle recipe);
bool mel_frame_recipe_binding_at(Mel_Frame_Recipe_Handle recipe, u32 index, Mel_Frame_Recipe_Binding_Desc* out);
