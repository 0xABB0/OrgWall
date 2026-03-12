#include "render.frame_plan.h"
#include "render.view.h"
#include "render.source.h"
#include "render.target.h"
#include "render.graph.h"
#include "swapchain.h"
#include "core.engine.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.vec4.h"

typedef struct {
    Mel_Swapchain_Handle swapchain;
    Mel_Render_Target target;
} Mel_Frame_Plan_Target;

typedef struct {
    Mel_Frame_Recipe_Binding_Desc binding;
    u32 authored_index;
} Mel_Frame_Plan_Binding;

struct Mel_Frame_Plan {
    str8 name;
    const Mel_Alloc* alloc;
    Mel_Gpu_Device* dev;
    Mel_Render_Graph* graph;
    Mel_Array(str8) generated_pass_names;
    Mel_Array(Mel_Frame_Plan_Target) generated_targets;
    Mel_Array(Mel_Frame_Plan_Resolved_Technique) resolved_techniques;
};

static Mel_SlotMap s_plans;
static bool s_initialized;

__attribute__((constructor(213)))
static void mel__frame_plan_registry_init(void)
{
    mel_slotmap_init(&s_plans, mel_alloc_heap(),
        .item_size = sizeof(Mel_Frame_Plan), .initial_capacity = 8);
    s_initialized = true;
}

__attribute__((destructor(213)))
static void mel__frame_plan_registry_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Frame_Plan* plans = mel_slotmap_data(&s_plans);
    u32 count = mel_slotmap_count(&s_plans);
    for (u32 i = 0; i < count; i++)
    {
        for (usize j = 0; j < plans[i].generated_pass_names.count; j++)
            mel_dealloc(plans[i].alloc, plans[i].generated_pass_names.items[j].data);
        for (usize j = 0; j < plans[i].generated_targets.count; j++)
            mel_render_target_shutdown(&plans[i].generated_targets.items[j].target);
        for (usize j = 0; j < plans[i].resolved_techniques.count; j++)
            mel_dealloc(plans[i].alloc, plans[i].resolved_techniques.items[j].technique_name.data);

        mel_array_free(&plans[i].generated_pass_names);
        mel_array_free(&plans[i].generated_targets);
        mel_array_free(&plans[i].resolved_techniques);
    }

    mel_slotmap_free(&s_plans);
    s_initialized = false;
}

static Mel_Frame_Plan* mel__frame_plan_get(Mel_Frame_Plan_Handle handle)
{
    assert(s_initialized);
    Mel_Frame_Plan* plan = mel_slotmap_get(&s_plans, handle.handle);
    assert(plan != nullptr);
    return plan;
}

static bool mel__same_swapchain(Mel_Swapchain_Handle a, Mel_Swapchain_Handle b)
{
    return a.handle.index == b.handle.index &&
        a.handle.generation == b.handle.generation;
}

static bool mel__graph_is_live(Mel_Render_Graph* graph)
{
    return graph != nullptr && graph->alloc != nullptr;
}

static void mel__frame_plan_clear_generated(Mel_Frame_Plan* plan)
{
    if (mel__graph_is_live(plan->graph))
    {
        for (usize i = 0; i < plan->generated_pass_names.count; i++)
            mel_render_graph_remove_pass(plan->graph, plan->generated_pass_names.items[i]);
    }

    for (usize i = 0; i < plan->generated_pass_names.count; i++)
        mel_dealloc(plan->alloc, plan->generated_pass_names.items[i].data);
    mel_array_clear(&plan->generated_pass_names);

    for (usize i = 0; i < plan->generated_targets.count; i++)
        mel_render_target_shutdown(&plan->generated_targets.items[i].target);
    mel_array_clear(&plan->generated_targets);

    for (usize i = 0; i < plan->resolved_techniques.count; i++)
        mel_dealloc(plan->alloc, plan->resolved_techniques.items[i].technique_name.data);
    mel_array_clear(&plan->resolved_techniques);
    plan->graph = nullptr;
}

static Mel_Frame_Plan_Target* mel__frame_plan_get_target(Mel_Frame_Plan* plan,
    Mel_Swapchain_Handle handle, Mel_Gpu_Device* dev)
{
    for (usize i = 0; i < plan->generated_targets.count; i++)
        if (mel__same_swapchain(plan->generated_targets.items[i].swapchain, handle))
            return &plan->generated_targets.items[i];

    Mel_Swapchain* sc = &mel_swapchain_registry_get(handle)->swapchain;
    Mel_Frame_Plan_Target gen = {
        .swapchain = handle,
    };
    mel_render_target_init_swapchain(&gen.target, sc, dev, S8("recipe_swapchain"));
    mel_array_push(&plan->generated_targets, gen);
    return &mel_array_last(&plan->generated_targets);
}

Mel_Render_List** mel_frame_plan_collect_render_lists(Mel_Frame_Plan_Handle handle,
    Mel_View_Handle view, u32 schema)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    u32 source_count = mel_view_source_count(view);
    Mel_Render_List** lists = mel_alloc(plan->alloc, sizeof(Mel_Render_List*) * (source_count + 1));
    u32 list_count = 0;

    for (u32 i = 0; i < source_count; i++)
    {
        Mel_Source_Handle source = mel_view_source_at(view, i);
        if (mel_source_kind(source) != MEL_SOURCE_LIST &&
            mel_source_kind(source) != MEL_SOURCE_RETAINED)
            continue;

        if (mel_source_schema(source) != schema)
            continue;

        Mel_Render_List* list = mel_source_render_list(source);
        if (!list)
            continue;
        lists[list_count++] = list;
    }

    lists[list_count] = nullptr;
    if (list_count == 0)
    {
        mel_dealloc(plan->alloc, lists);
        return nullptr;
    }
    return lists;
}

void mel_frame_plan_free_read_lists(Mel_Frame_Plan_Handle handle, Mel_Render_List** lists)
{
    if (!lists)
        return;
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    mel_dealloc(plan->alloc, lists);
}

static bool mel__frame_plan_binding_before(Mel_Frame_Plan_Binding a, Mel_Frame_Plan_Binding b)
{
    if (mel__same_swapchain(a.binding.swapchain, b.binding.swapchain))
    {
        if (a.binding.order != b.binding.order)
            return a.binding.order < b.binding.order;
    }
    return a.authored_index < b.authored_index;
}

static Mel_Frame_Plan_Binding* mel__frame_plan_build_sorted_bindings(Mel_Frame_Plan* plan,
    Mel_Frame_Recipe_Handle recipe, u32* out_count)
{
    u32 binding_count = mel_frame_recipe_binding_count(recipe);
    Mel_Frame_Plan_Binding* bindings = nullptr;

    if (binding_count > 0)
        bindings = mel_alloc(plan->alloc, sizeof(Mel_Frame_Plan_Binding) * binding_count);

    for (u32 i = 0; i < binding_count; i++)
    {
        Mel_Frame_Recipe_Binding_Desc binding = {0};
        bool found = mel_frame_recipe_binding_at(recipe, i, &binding);
        assert(found);

        Mel_Frame_Plan_Binding item = {
            .binding = binding,
            .authored_index = i,
        };

        u32 insert_at = i;
        while (insert_at > 0 && mel__frame_plan_binding_before(item, bindings[insert_at - 1]))
        {
            bindings[insert_at] = bindings[insert_at - 1];
            insert_at--;
        }
        bindings[insert_at] = item;
    }

    *out_count = binding_count;
    return bindings;
}

static u32 mel__frame_plan_effective_composition_mode(Mel_Frame_Recipe_Binding_Desc binding)
{
    if (binding.overlay)
        return MEL_VIEW_COMPOSE_ALPHA;
    return mel_view_composition_mode(binding.view);
}

Mel_Frame_Plan_Handle mel_frame_plan_create(str8 name)
{
    assert(s_initialized);

    Mel_Frame_Plan plan = {
        .name = str8_dup(name, mel_alloc_heap()),
        .alloc = mel_alloc_heap(),
    };
    mel_array_init(&plan.generated_pass_names, plan.alloc);
    mel_array_init(&plan.generated_targets, plan.alloc);
    mel_array_init(&plan.resolved_techniques, plan.alloc);

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_plans, &plan);
    return (Mel_Frame_Plan_Handle){ .handle = raw };
}

void mel_frame_plan_destroy(Mel_Frame_Plan_Handle handle)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    mel__frame_plan_clear_generated(plan);
    mel_array_free(&plan->generated_pass_names);
    mel_array_free(&plan->generated_targets);
    mel_array_free(&plan->resolved_techniques);
    mel_dealloc(plan->alloc, plan->name.data);
    mel_slotmap_remove(&s_plans, handle.handle);
}

bool mel_frame_plan_add_render_list_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(ctx->plan);
    const Mel_Camera* camera = mel_view_camera(ctx->binding.view);
    if (camera == nullptr)
        return false;

    VkAttachmentLoadOp load_op = (!*ctx->wrote_any_pass && (ctx->first_for_swapchain || ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->binding.view)
        ? mel_view_clear_color(ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);

    str8 pass_name = str8_fmt(plan->alloc, "recipe.%.*s.%.*s.%u.%.*s",
        (int)plan->name.len, plan->name.data,
        (int)ctx->recipe_name.len, ctx->recipe_name.data,
        ctx->binding_index,
        (int)pass_suffix.len, pass_suffix.data);
    mel_array_push(&plan->generated_pass_names, pass_name);

    mel_render_graph_add_pass(ctx->graph, pass_name,
        .fn = fn,
        .user = user,
        .camera = (Mel_Camera*)camera,
        .read_lists = read_lists,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = ctx->target, .load_op = load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } }));
    *ctx->wrote_any_pass = true;
    return true;
}

bool mel_frame_plan_compile_opt(Mel_Frame_Plan_Handle handle, Mel_Frame_Recipe_Handle recipe_handle,
    Mel_Frame_Plan_Compile_Opt opt)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    Mel_Render_Graph* graph = opt.graph;
    Mel_Gpu_Device* dev = opt.dev ? opt.dev : mel_gpu_dev();
    str8 recipe_name = mel_frame_recipe_name(recipe_handle);
    u32 binding_count = 0;
    Mel_Frame_Plan_Binding* bindings = nullptr;

    assert(graph != nullptr);
    assert(dev != nullptr);

    mel__frame_plan_clear_generated(plan);
    plan->graph = graph;
    plan->dev = dev;
    bindings = mel__frame_plan_build_sorted_bindings(plan, recipe_handle, &binding_count);

    for (u32 i = 0; i < binding_count; i++)
    {
        Mel_Frame_Recipe_Binding_Desc binding = bindings[i].binding;
        Mel_Frame_Plan_Target* gen_target = mel__frame_plan_get_target(plan, binding.swapchain, dev);
        bool first_for_swapchain = true;
        for (u32 j = 0; j < i; j++)
        {
            if (mel__same_swapchain(bindings[j].binding.swapchain, binding.swapchain))
            {
                first_for_swapchain = false;
                break;
            }
        }

        bool replace_contents = mel__frame_plan_effective_composition_mode(binding) == MEL_VIEW_COMPOSE_REPLACE;
        bool wrote_any_pass = false;
        u32 technique_count = mel_frame_recipe_technique_count_for_view(recipe_handle, binding.view);

        for (u32 technique_index = 0; technique_index < technique_count; technique_index++)
        {
            Mel_Technique_Family_Id family = MEL_TECHNIQUE_NONE;
            bool found = mel_frame_recipe_technique_at_for_view(recipe_handle, binding.view, technique_index, &family);
            assert(found);

            const Mel_Technique_Desc* technique = mel_render_technique_get(family);
            if (!technique)
                goto fail;

            Mel_Frame_Plan_Technique_Ctx technique_ctx = {
                .plan = handle,
                .recipe = recipe_handle,
                .binding = binding,
                .binding_index = i,
                .recipe_name = recipe_name,
                .graph = graph,
                .dev = dev,
                .target = &gen_target->target,
                .opt = opt,
                .first_for_swapchain = first_for_swapchain,
                .replace_contents = replace_contents,
                .wrote_any_pass = &wrote_any_pass,
            };
            Mel_Technique_Compile_Ctx compile_ctx = {
                .plan_ctx = &technique_ctx,
                .technique = technique,
            };

            Mel_Technique_Compile_Result result = technique->compile(&compile_ctx);
            if (result == MEL_TECHNIQUE_COMPILE_FAIL)
                goto fail;
            if (result == MEL_TECHNIQUE_COMPILE_CONTRIBUTED)
            {
                mel_array_push(&plan->resolved_techniques, ((Mel_Frame_Plan_Resolved_Technique){
                    .view = binding.view,
                    .family = family,
                    .technique_name = str8_dup(technique->name, plan->alloc),
                    .binding_index = i,
                }));
            }
        }
    }

    if (!mel_render_graph_compile(graph))
        goto fail;

    if (bindings)
        mel_dealloc(plan->alloc, bindings);
    return true;

fail:
    if (bindings)
        mel_dealloc(plan->alloc, bindings);
    mel__frame_plan_clear_generated(plan);
    return false;
}

Mel_Render_Target* mel_frame_plan_swapchain_target(Mel_Frame_Plan_Handle handle, Mel_Swapchain_Handle swapchain)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    Mel_Gpu_Device* dev = plan->dev ? plan->dev : mel_gpu_dev();
    Mel_Frame_Plan_Target* target = mel__frame_plan_get_target(plan, swapchain, dev);
    return target ? &target->target : nullptr;
}

u32 mel_frame_plan_resolved_technique_count(Mel_Frame_Plan_Handle handle)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    return (u32)plan->resolved_techniques.count;
}

bool mel_frame_plan_resolved_technique_at(Mel_Frame_Plan_Handle handle, u32 index, Mel_Frame_Plan_Resolved_Technique* out)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    if (index >= (u32)plan->resolved_techniques.count)
        return false;
    if (out)
        *out = plan->resolved_techniques.items[index];
    return true;
}
