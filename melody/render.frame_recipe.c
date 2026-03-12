#include "render.frame_recipe.h"
#include "render.view.h"
#include "render.source.h"
#include "render.target.h"
#include "render.graph.h"
#include "core.engine.h"
#include "sprite.pass.h"
#include "swapchain.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.vec4.h"

typedef struct {
    Mel_View_Handle view;
    Mel_Technique_Family_Id family;
} Mel_Frame_Recipe_Technique;

typedef struct {
    Mel_View_Handle view;
    Mel_Swapchain_Handle swapchain;
    bool overlay;
} Mel_Frame_Recipe_Binding;

typedef struct {
    Mel_Swapchain_Handle swapchain;
    Mel_Render_Target target;
} Mel_Frame_Recipe_Target;

struct Mel_Frame_Recipe {
    str8 name;
    const Mel_Alloc* alloc;
    Mel_Array(Mel_Frame_Recipe_Technique) techniques;
    Mel_Array(Mel_Frame_Recipe_Binding) bindings;
    Mel_Array(str8) generated_pass_names;
    Mel_Array(Mel_Frame_Recipe_Target) generated_targets;
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
        for (usize j = 0; j < recipes[i].generated_pass_names.count; j++)
            mel_dealloc(recipes[i].alloc, recipes[i].generated_pass_names.items[j].data);
        for (usize j = 0; j < recipes[i].generated_targets.count; j++)
            mel_render_target_shutdown(&recipes[i].generated_targets.items[j].target);

        mel_array_free(&recipes[i].techniques);
        mel_array_free(&recipes[i].bindings);
        mel_array_free(&recipes[i].generated_pass_names);
        mel_array_free(&recipes[i].generated_targets);
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

static bool mel__same_swapchain(Mel_Swapchain_Handle a, Mel_Swapchain_Handle b)
{
    return a.handle.index == b.handle.index &&
        a.handle.generation == b.handle.generation;
}

static bool mel__view_uses_family(Mel_Frame_Recipe* recipe, Mel_View_Handle view, Mel_Technique_Family_Id family)
{
    for (usize i = 0; i < recipe->techniques.count; i++)
        if (mel__same_view(recipe->techniques.items[i].view, view) &&
            recipe->techniques.items[i].family == family)
            return true;
    return false;
}

static void mel__frame_recipe_clear_generated(Mel_Frame_Recipe* recipe, Mel_Render_Graph* graph)
{
    for (usize i = 0; i < recipe->generated_pass_names.count; i++)
    {
        if (graph)
            mel_render_graph_remove_pass(graph, recipe->generated_pass_names.items[i]);
        mel_dealloc(recipe->alloc, recipe->generated_pass_names.items[i].data);
    }
    mel_array_clear(&recipe->generated_pass_names);

    for (usize i = 0; i < recipe->generated_targets.count; i++)
        mel_render_target_shutdown(&recipe->generated_targets.items[i].target);
    mel_array_clear(&recipe->generated_targets);
}

static Mel_Frame_Recipe_Target* mel__frame_recipe_get_target(Mel_Frame_Recipe* recipe,
    Mel_Swapchain_Handle handle, Mel_Gpu_Device* dev)
{
    for (usize i = 0; i < recipe->generated_targets.count; i++)
        if (mel__same_swapchain(recipe->generated_targets.items[i].swapchain, handle))
            return &recipe->generated_targets.items[i];

    Mel_Swapchain* sc = &mel_swapchain_registry_get(handle)->swapchain;
    Mel_Frame_Recipe_Target gen = {
        .swapchain = handle,
    };
    mel_render_target_init_swapchain(&gen.target, sc, dev, S8("recipe_swapchain"));
    mel_array_push(&recipe->generated_targets, gen);
    return &mel_array_last(&recipe->generated_targets);
}

static Mel_Render_List** mel__frame_recipe_build_sprite_lists(Mel_Frame_Recipe* recipe,
    Mel_View_Handle view)
{
    u32 source_count = mel_view_source_count(view);
    Mel_Render_List** lists = mel_alloc(recipe->alloc, sizeof(Mel_Render_List*) * (source_count + 1));
    u32 list_count = 0;

    for (u32 i = 0; i < source_count; i++)
    {
        Mel_Source_Handle source = mel_view_source_at(view, i);
        if (mel_source_kind(source) != MEL_SOURCE_LIST &&
            mel_source_kind(source) != MEL_SOURCE_RETAINED)
            continue;

        if (mel_source_schema(source) != MEL_SCHEMA_SPRITE)
            continue;

        Mel_Render_List* list = mel_source_render_list(source);
        if (!list) continue;
        lists[list_count++] = list;
    }

    lists[list_count] = nullptr;

    if (list_count == 0)
    {
        mel_dealloc(recipe->alloc, lists);
        return nullptr;
    }

    return lists;
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
    mel_array_init(&recipe.generated_pass_names, recipe.alloc);
    mel_array_init(&recipe.generated_targets, recipe.alloc);

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_recipes, &recipe);
    return (Mel_Frame_Recipe_Handle){ .handle = raw };
}

void mel_frame_recipe_destroy(Mel_Frame_Recipe_Handle handle)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    mel__frame_recipe_clear_generated(recipe, nullptr);
    mel_array_free(&recipe->techniques);
    mel_array_free(&recipe->bindings);
    mel_array_free(&recipe->generated_pass_names);
    mel_array_free(&recipe->generated_targets);
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
    Mel_Swapchain_Handle swapchain, bool overlay)
{
    for (usize i = 0; i < recipe->bindings.count; i++)
    {
        if (mel__same_view(recipe->bindings.items[i].view, view) &&
            mel__same_swapchain(recipe->bindings.items[i].swapchain, swapchain))
        {
            recipe->bindings.items[i].overlay = overlay;
            return;
        }
    }

    mel_array_push(&recipe->bindings, ((Mel_Frame_Recipe_Binding){
        .view = view,
        .swapchain = swapchain,
        .overlay = overlay,
    }));
}

void mel_frame_recipe_present(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Swapchain_Handle swapchain)
{
    mel__frame_recipe_bind(mel__frame_recipe_get(handle), view, swapchain, false);
}

void mel_frame_recipe_overlay(Mel_Frame_Recipe_Handle handle, Mel_View_Handle view, Mel_Swapchain_Handle swapchain)
{
    mel__frame_recipe_bind(mel__frame_recipe_get(handle), view, swapchain, true);
}

bool mel_frame_recipe_compile_opt(Mel_Frame_Recipe_Handle handle, Mel_Frame_Recipe_Compile_Opt opt)
{
    Mel_Frame_Recipe* recipe = mel__frame_recipe_get(handle);
    Mel_Render_Graph* graph = opt.graph;
    Mel_Gpu_Device* dev = opt.dev ? opt.dev : mel_gpu_dev();
    Mel_Sprite_Pass* sprite_pass = opt.sprite_pass ? opt.sprite_pass : mel_sprite_pass();

    assert(graph != nullptr);
    assert(dev != nullptr);
    assert(sprite_pass != nullptr);

    mel__frame_recipe_clear_generated(recipe, graph);

    for (usize i = 0; i < recipe->bindings.count; i++)
    {
        Mel_Frame_Recipe_Binding* binding = &recipe->bindings.items[i];

        if (!mel__view_uses_family(recipe, binding->view, MEL_TECHNIQUE_SPRITE))
            continue;

        const Mel_Camera* camera = mel_view_camera(binding->view);
        if (camera == nullptr)
            return false;

        Mel_Render_List** read_lists = mel__frame_recipe_build_sprite_lists(recipe, binding->view);
        if (!read_lists)
            return false;

        Mel_Frame_Recipe_Target* gen_target = mel__frame_recipe_get_target(recipe, binding->swapchain, dev);
        bool first_for_swapchain = true;
        for (usize j = 0; j < i; j++)
        {
            if (mel__same_swapchain(recipe->bindings.items[j].swapchain, binding->swapchain))
            {
                first_for_swapchain = false;
                break;
            }
        }

        VkAttachmentLoadOp load_op = first_for_swapchain
            ? VK_ATTACHMENT_LOAD_OP_CLEAR
            : VK_ATTACHMENT_LOAD_OP_LOAD;

        Mel_Vec4 clear = mel_view_clear_color_enabled(binding->view)
            ? mel_view_clear_color(binding->view)
            : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);

        str8 pass_name = str8_fmt(recipe->alloc, "recipe.%.*s.%zu",
            (int)recipe->name.len, recipe->name.data, i);
        mel_array_push(&recipe->generated_pass_names, pass_name);

        mel_render_graph_add_pass(graph, pass_name,
            .fn = mel_sprite_pass_execute,
            .user = sprite_pass,
            .camera = (Mel_Camera*)camera,
            .read_lists = read_lists,
            .write_targets = MEL_WRITE_TARGETS(
                { .target = &gen_target->target, .load_op = load_op,
                  .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } }));
        mel_dealloc(recipe->alloc, read_lists);
    }

    return mel_render_graph_compile(graph);
}
