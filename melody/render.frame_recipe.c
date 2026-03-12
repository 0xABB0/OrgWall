#include "render.frame_recipe.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"

typedef struct {
    Mel_View_Handle view;
    Mel_Technique_Family_Id family;
} Mel_Frame_Recipe_Technique;

struct Mel_Frame_Recipe {
    str8 name;
    const Mel_Alloc* alloc;
    Mel_Array(Mel_Frame_Recipe_Technique) techniques;
    Mel_Array(Mel_Frame_Recipe_Binding_Desc) bindings;
    i32 next_binding_order;
};

static Mel_SlotMap s_recipes;
static bool s_initialized;

__attribute__((constructor(212)))
static void mel__frame_recipe_registry_init(void)
{
    mel_slotmap_init(&s_recipes, mel_alloc_heap(),
        .item_size = sizeof(Mel_Frame_Recipe), .initial_capacity = 8);
    s_initialized = true;
}

__attribute__((destructor(212)))
static void mel__frame_recipe_registry_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Frame_Recipe* recipes = mel_slotmap_data(&s_recipes);
    u32 count = mel_slotmap_count(&s_recipes);
    for (u32 i = 0; i < count; i++)
    {
        mel_array_free(&recipes[i].techniques);
        mel_array_free(&recipes[i].bindings);
    }

    mel_slotmap_free(&s_recipes);
    s_initialized = false;
}

static Mel_Frame_Recipe* mel__frame_recipe_get(Mel_Frame_Recipe_Handle handle)
{
    assert(s_initialized);
    Mel_Frame_Recipe* recipe = mel_slotmap_get(&s_recipes, handle.handle);
    assert(recipe != nullptr);
    return recipe;
}

static bool mel__same_view(Mel_View_Handle a, Mel_View_Handle b)
{
    return a.handle.index == b.handle.index &&
        a.handle.generation == b.handle.generation;
}

Mel_Frame_Recipe_Handle mel_frame_recipe_create(str8 name)
{
    assert(s_initialized);

    Mel_Frame_Recipe recipe = {
        .name = str8_dup(name, mel_alloc_heap()),
        .alloc = mel_alloc_heap(),
    };
    mel_array_init(&recipe.techniques, recipe.alloc);
    mel_array_init(&recipe.bindings, recipe.alloc);

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_recipes, &recipe);
    return (Mel_Frame_Recipe_Handle){ .handle = raw };
}

void mel_frame_recipe_destroy(Mel_Frame_Recipe_Handle handle)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    mel_array_free(&recipe->techniques);
    mel_array_free(&recipe->bindings);
    mel_dealloc(recipe->alloc, recipe->name.data);
    mel_slotmap_remove(&s_recipes, handle.handle);
}

void mel_frame_recipe_use_technique(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Technique_Family_Id family)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);

    for (usize i = 0; i < recipe->techniques.count; i++)
        if (mel__same_view(recipe->techniques.items[i].view, view) &&
            recipe->techniques.items[i].family == family)
            return;

    mel_array_push(&recipe->techniques, ((Mel_Frame_Recipe_Technique){
        .view = view,
        .family = family,
    }));
}

void mel_frame_recipe_disable_technique(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Technique_Family_Id family)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);

    for (usize i = 0; i < recipe->techniques.count; i++)
    {
        if (mel__same_view(recipe->techniques.items[i].view, view) &&
            recipe->techniques.items[i].family == family)
        {
            mel_array_remove_ordered(&recipe->techniques, i);
            return;
        }
    }
}

static void mel__frame_recipe_bind(Mel_Frame_Recipe* recipe, Mel_View_Handle view,
    Mel_Swapchain_Handle swapchain, bool overlay, i32 order)
{
    for (usize i = 0; i < recipe->bindings.count; i++)
    {
        if (mel__same_view(recipe->bindings.items[i].view, view) &&
            recipe->bindings.items[i].swapchain.handle.index == swapchain.handle.index &&
            recipe->bindings.items[i].swapchain.handle.generation == swapchain.handle.generation)
        {
            recipe->bindings.items[i].overlay = overlay;
            recipe->bindings.items[i].order = order;
            return;
        }
    }

    mel_array_push(&recipe->bindings, ((Mel_Frame_Recipe_Binding_Desc){
        .view = view,
        .swapchain = swapchain,
        .overlay = overlay,
        .order = order,
    }));
}

void mel_frame_recipe_present(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Swapchain_Handle swapchain)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    mel__frame_recipe_bind(recipe, view, swapchain, false, recipe->next_binding_order++);
}

void mel_frame_recipe_overlay(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Swapchain_Handle swapchain)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    mel__frame_recipe_bind(recipe, view, swapchain, true, recipe->next_binding_order++);
}

void mel_frame_recipe_present_ordered(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Swapchain_Handle swapchain, i32 order)
{
    mel__frame_recipe_bind(mel__frame_recipe_get(handle), view, swapchain, false, order);
}

void mel_frame_recipe_overlay_ordered(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Swapchain_Handle swapchain, i32 order)
{
    mel__frame_recipe_bind(mel__frame_recipe_get(handle), view, swapchain, true, order);
}

str8 mel_frame_recipe_name(Mel_Frame_Recipe_Handle handle)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    return recipe->name;
}

bool mel_frame_recipe_uses_technique(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Technique_Family_Id family)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    for (usize i = 0; i < recipe->techniques.count; i++)
        if (mel__same_view(recipe->techniques.items[i].view, view) &&
            recipe->techniques.items[i].family == family)
            return true;
    return false;
}

u32 mel_frame_recipe_technique_count_for_view(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    u32 count = 0;
    for (usize i = 0; i < recipe->techniques.count; i++)
        if (mel__same_view(recipe->techniques.items[i].view, view))
            count++;
    return count;
}

bool mel_frame_recipe_technique_at_for_view(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, u32 index, Mel_Technique_Family_Id* out)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    u32 count = 0;
    for (usize i = 0; i < recipe->techniques.count; i++)
    {
        if (!mel__same_view(recipe->techniques.items[i].view, view))
            continue;
        if (count == index)
        {
            if (out)
                *out = recipe->techniques.items[i].family;
            return true;
        }
        count++;
    }
    return false;
}

u32 mel_frame_recipe_binding_count(Mel_Frame_Recipe_Handle handle)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    return (u32)recipe->bindings.count;
}

bool mel_frame_recipe_binding_at(Mel_Frame_Recipe_Handle handle, u32 index, Mel_Frame_Recipe_Binding_Desc* out)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    if (index >= (u32)recipe->bindings.count)
        return false;
    if (out)
        *out = recipe->bindings.items[index];
    return true;
}
