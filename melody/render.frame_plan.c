#include "render.frame_plan.h"
#include "render.view.h"
#include "render.source.h"
#include "render.target.h"
#include "render.graph.h"
#include "swapchain.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "collection.slotmap.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.vec4.h"

typedef struct {
    Mel_Swapchain_Handle swapchain;
    u32 role;
    Mel_Render_Target target;
} Mel_Frame_Plan_Target;

typedef struct {
    Mel_Frame_Plan_Resolved_Technique public_info;
    u32 pass_start;
    u32 pass_count;
    u32 source_snapshot_start;
    u32 source_snapshot_count;
    u64 parameter_version;
    u64 topology_version;
} Mel_Frame_Plan_Resolved_Record;

typedef struct {
    Mel_Frame_Plan_Technique_Diagnostic public_info;
} Mel_Frame_Plan_Diagnostic_Record;

typedef struct {
    Mel_Frame_Recipe_Binding_Desc binding;
    u32 authored_index;
} Mel_Frame_Plan_Binding;

typedef struct {
    Mel_Source_Handle source;
    u64 shape_version;
} Mel_Frame_Plan_Source_Snapshot;

struct Mel_Frame_Plan {
    str8 name;
    const Mel_Alloc* alloc;
    Mel_Gpu_Device* dev;
    Mel_Render_Graph* graph;
    Mel_Array(str8) generated_pass_names;
    Mel_Array(Mel_Frame_Plan_Target) generated_targets;
    Mel_Array(Mel_Frame_Plan_Resolved_Record) resolved_techniques;
    Mel_Array(Mel_Frame_Plan_Diagnostic_Record) diagnostics;
    Mel_Array(Mel_Frame_Plan_Source_Snapshot) source_snapshots;
    u32 dirty_flags;
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
            mel_dealloc(plans[i].alloc, plans[i].resolved_techniques.items[j].public_info.technique_name.data);
        for (usize j = 0; j < plans[i].diagnostics.count; j++)
        {
            mel_dealloc(plans[i].alloc, plans[i].diagnostics.items[j].public_info.technique_name.data);
            mel_dealloc(plans[i].alloc, plans[i].diagnostics.items[j].public_info.reason.data);
        }

        mel_array_free(&plans[i].generated_pass_names);
        mel_array_free(&plans[i].generated_targets);
        mel_array_free(&plans[i].resolved_techniques);
        mel_array_free(&plans[i].diagnostics);
        mel_array_free(&plans[i].source_snapshots);
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
        if (plan->graph->dev && plan->graph->dev->device != VK_NULL_HANDLE)
            mel_gpu_device_wait_idle(plan->graph->dev);

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
        mel_dealloc(plan->alloc, plan->resolved_techniques.items[i].public_info.technique_name.data);
    mel_array_clear(&plan->resolved_techniques);
    for (usize i = 0; i < plan->diagnostics.count; i++)
    {
        mel_dealloc(plan->alloc, plan->diagnostics.items[i].public_info.technique_name.data);
        mel_dealloc(plan->alloc, plan->diagnostics.items[i].public_info.reason.data);
    }
    mel_array_clear(&plan->diagnostics);
    mel_array_clear(&plan->source_snapshots);
    plan->graph = nullptr;
    plan->dirty_flags = MEL_FRAME_DIRTY_NONE;
}

static Mel_Frame_Plan_Target* mel__frame_plan_get_target(Mel_Frame_Plan* plan,
    Mel_Swapchain_Handle handle, Mel_Gpu_Device* dev, u32 role)
{
    for (usize i = 0; i < plan->generated_targets.count; i++)
        if (mel__same_swapchain(plan->generated_targets.items[i].swapchain, handle) &&
            plan->generated_targets.items[i].role == role)
            return &plan->generated_targets.items[i];

    Mel_Swapchain* sc = &mel_swapchain_registry_get(handle)->swapchain;
    Mel_Frame_Plan_Target gen = {
        .swapchain = handle,
        .role = role,
    };
    if (role == MEL_RENDER_TARGET_DEPTH)
    {
        if (dev && dev->device != VK_NULL_HANDLE)
        {
            mel_render_target_init(&gen.target, dev,
                .name = S8("recipe_depth"),
                .width = sc->extent.width,
                .height = sc->extent.height,
                .format = VK_FORMAT_D32_SFLOAT);
        }
        else
        {
            gen.target = (Mel_Render_Target){
                .name = S8("recipe_depth"),
                .kind = MEL_RENDER_TARGET_DEPTH,
                .width = sc->extent.width,
                .height = sc->extent.height,
                .format = VK_FORMAT_D32_SFLOAT,
                .dev = dev,
                .alloc = plan->alloc,
            };
        }
    }
    else
    {
        mel_render_target_init_swapchain(&gen.target, sc, dev, S8("recipe_swapchain"));
    }
    mel_array_push(&plan->generated_targets, gen);
    return &mel_array_last(&plan->generated_targets);
}

static bool mel__frame_plan_refresh_target(Mel_Frame_Plan* plan, Mel_Frame_Plan_Target* gen, Mel_Gpu_Device* dev)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(gen->swapchain)->swapchain;
    bool changed = false;

    if (gen->target.kind == MEL_RENDER_TARGET_SWAPCHAIN)
    {
        gen->target.width = sc->extent.width;
        gen->target.height = sc->extent.height;
        gen->target.format = sc->format;
        return true;
    }

    if (gen->target.width == sc->extent.width &&
        gen->target.height == sc->extent.height)
        return true;

    changed = true;

    if (dev && dev->device != VK_NULL_HANDLE)
    {
        str8 name = gen->target.name;
        VkFormat format = gen->target.format;
        const Mel_Alloc* alloc = gen->target.alloc ? gen->target.alloc : plan->alloc;
        mel_render_target_shutdown(&gen->target);
        mel_render_target_init(&gen->target, dev,
            .name = name,
            .width = sc->extent.width,
            .height = sc->extent.height,
            .format = format,
            .alloc = alloc);
    }
    else
    {
        gen->target.width = sc->extent.width;
        gen->target.height = sc->extent.height;
        gen->target.format = gen->role == MEL_RENDER_TARGET_DEPTH ? VK_FORMAT_D32_SFLOAT : sc->format;
        gen->target.dev = dev;
    }

    return !changed || (gen->target.width == sc->extent.width && gen->target.height == sc->extent.height);
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

Mel_Source_Handle* mel_frame_plan_collect_sources(Mel_Frame_Plan_Handle handle,
    Mel_View_Handle view, Mel_Source_Kind kind, u32 schema)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    u32 source_count = mel_view_source_count(view);
    Mel_Source_Handle* sources = mel_alloc(plan->alloc, sizeof(Mel_Source_Handle) * (source_count + 1));
    u32 collected = 0;

    for (u32 i = 0; i < source_count; i++)
    {
        Mel_Source_Handle source = mel_view_source_at(view, i);
        if (mel_source_kind(source) != kind)
            continue;
        if (mel_source_schema(source) != schema)
            continue;
        sources[collected++] = source;
    }

    sources[collected] = MEL_SOURCE_HANDLE_NULL;
    if (collected == 0)
    {
        mel_dealloc(plan->alloc, sources);
        return nullptr;
    }
    return sources;
}

void mel_frame_plan_free_read_lists(Mel_Frame_Plan_Handle handle, Mel_Render_List** lists)
{
    if (!lists)
        return;
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    mel_dealloc(plan->alloc, lists);
}

void mel_frame_plan_free_read_sources(Mel_Frame_Plan_Handle handle, Mel_Source_Handle* sources)
{
    if (!sources)
        return;
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    mel_dealloc(plan->alloc, sources);
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

static Mel_Render_Graph_Pass* mel__frame_plan_find_graph_pass(Mel_Render_Graph* graph, str8 name)
{
    if (!graph)
        return nullptr;

    for (usize i = 0; i < graph->passes.count; i++)
        if (str8_equals(graph->passes.items[i].name, name))
            return &graph->passes.items[i];

    return nullptr;
}

bool mel_frame_plan_refresh_contributed_passes(Mel_Frame_Plan_Handle handle, Mel_View_Handle view,
    str8* pass_names, u32 pass_count)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    const Mel_Camera* camera = mel_view_camera(view);
    u32 viewport_mode = mel_view_target_mode(view) == MEL_VIEW_TARGET_FIT
        ? MEL_PASS_VIEWPORT_FIT
        : MEL_PASS_VIEWPORT_TARGET;
    u32 design_width = mel_view_design_width(view);
    u32 design_height = mel_view_design_height(view);
    Mel_Vec4 clear = mel_view_clear_color_enabled(view)
        ? mel_view_clear_color(view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);

    for (u32 i = 0; i < pass_count; i++)
    {
        Mel_Render_Graph_Pass* pass = mel__frame_plan_find_graph_pass(plan->graph, pass_names[i]);
        if (!pass)
            return false;

        pass->camera = (Mel_Camera*)camera;
        pass->viewport_mode = viewport_mode;
        pass->viewport_design_width = design_width;
        pass->viewport_design_height = design_height;

        if (pass->write_targets)
        {
            for (Mel_Pass_Write_Target* wt = pass->write_targets; wt->target; wt++)
            {
                if (wt->target->kind == MEL_RENDER_TARGET_DEPTH)
                    continue;
                wt->clear.color.r = clear.x;
                wt->clear.color.g = clear.y;
                wt->clear.color.b = clear.z;
                wt->clear.color.a = clear.w;
            }
        }
    }

    return true;
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
    mel_array_init(&plan.diagnostics, plan.alloc);
    mel_array_init(&plan.source_snapshots, plan.alloc);

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
    mel_array_free(&plan->diagnostics);
    mel_array_free(&plan->source_snapshots);
    mel_dealloc(plan->alloc, plan->name.data);
    mel_slotmap_remove(&s_plans, handle.handle);
}

bool mel_frame_plan_add_graphics_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists, Mel_Source_Handle* read_sources,
    Mel_Render_Target** read_targets, Mel_Pass_Write_Target* write_targets)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(ctx->plan);
    const Mel_Camera* camera = mel_view_camera(ctx->binding.view);

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
        .viewport_mode = mel_view_target_mode(ctx->binding.view) == MEL_VIEW_TARGET_FIT ? MEL_PASS_VIEWPORT_FIT : MEL_PASS_VIEWPORT_TARGET,
        .viewport_design_width = mel_view_design_width(ctx->binding.view),
        .viewport_design_height = mel_view_design_height(ctx->binding.view),
        .read_lists = read_lists,
        .read_sources = read_sources,
        .read_targets = read_targets,
        .write_targets = write_targets);
    *ctx->wrote_any_pass = true;
    return true;
}

bool mel_frame_plan_add_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists, Mel_Source_Handle* read_sources, Mel_Render_Target** read_targets)
{
    VkAttachmentLoadOp load_op = (!*ctx->wrote_any_pass && (ctx->first_for_swapchain || ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->binding.view)
        ? mel_view_clear_color(ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);

    return mel_frame_plan_add_graphics_pass(ctx, pass_suffix, fn, user, read_lists, read_sources, read_targets,
        MEL_WRITE_TARGETS(
            { .target = ctx->target, .load_op = load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } }));
}

bool mel_frame_plan_add_render_list_pass(Mel_Frame_Plan_Technique_Ctx* ctx, str8 pass_suffix,
    Mel_Render_Pass_Fn fn, void* user, Mel_Render_List** read_lists)
{
    const Mel_Camera* camera = mel_view_camera(ctx->binding.view);
    if (camera == nullptr)
        return false;
    return mel_frame_plan_add_pass(ctx, pass_suffix, fn, user, read_lists, nullptr, nullptr);
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
    plan->dirty_flags = MEL_FRAME_DIRTY_NONE;
    bindings = mel__frame_plan_build_sorted_bindings(plan, recipe_handle, &binding_count);

    for (u32 i = 0; i < binding_count; i++)
    {
        Mel_Frame_Recipe_Binding_Desc binding = bindings[i].binding;
        Mel_Frame_Plan_Target* gen_target = mel__frame_plan_get_target(plan, binding.swapchain, dev, MEL_RENDER_TARGET_SWAPCHAIN);
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
            const Mel_Technique_Desc* technique = nullptr;
            i32 selected_priority = 0;
            i32 selected_diag_index = -1;
            u32 family_variant_count = mel_render_technique_count_for_family(family);
            for (u32 variant_index = 0; variant_index < family_variant_count; variant_index++)
            {
                const Mel_Technique_Desc* candidate = mel_render_technique_at_for_family(family, variant_index);
                if (!candidate)
                    continue;

                Mel_Technique_Check_Result support = mel_render_technique_support(candidate, &technique_ctx);
                Mel_Technique_Check_Result match = support.ok
                    ? mel_render_technique_match(candidate, &technique_ctx)
                    : (Mel_Technique_Check_Result){
                        .ok = false,
                        .kind = MEL_TECHNIQUE_CHECK_POLICY_SKIPPED,
                        .reason = S8("skipped because capability check failed"),
                    };
                Mel_Technique_Check_Result diag_reason = support.ok ? match : support;
                mel_array_push(&plan->diagnostics, ((Mel_Frame_Plan_Diagnostic_Record){
                    .public_info = {
                        .view = binding.view,
                        .family = family,
                        .technique_name = str8_dup(candidate->name, plan->alloc),
                        .reason = str8_dup(diag_reason.reason, plan->alloc),
                        .binding_index = i,
                        .reason_kind = diag_reason.kind,
                        .supported = support.ok,
                        .matched = support.ok && match.ok,
                        .selected = false,
                    },
                }));

                if (support.ok && match.ok &&
                    (!technique || candidate->priority > selected_priority))
                {
                    if (selected_diag_index >= 0)
                    {
                        plan->diagnostics.items[selected_diag_index].public_info.selected = false;
                        plan->diagnostics.items[selected_diag_index].public_info.reason_kind = MEL_TECHNIQUE_CHECK_POLICY_SKIPPED;
                        mel_dealloc(plan->alloc, plan->diagnostics.items[selected_diag_index].public_info.reason.data);
                        plan->diagnostics.items[selected_diag_index].public_info.reason =
                            str8_dup(S8("matched but lower priority than selected variant"), plan->alloc);
                    }
                    technique = candidate;
                    selected_priority = candidate->priority;
                    selected_diag_index = (i32)plan->diagnostics.count - 1;
                    mel_array_last(&plan->diagnostics).public_info.selected = true;
                }
                else if (support.ok && match.ok)
                {
                    mel_array_last(&plan->diagnostics).public_info.reason_kind = MEL_TECHNIQUE_CHECK_POLICY_SKIPPED;
                    mel_dealloc(plan->alloc, mel_array_last(&plan->diagnostics).public_info.reason.data);
                    mel_array_last(&plan->diagnostics).public_info.reason =
                        str8_dup(S8("matched but lower priority than selected variant"), plan->alloc);
                }
            }
            if (!technique)
                continue;
            Mel_Technique_Compile_Ctx compile_ctx = {
                .plan_ctx = &technique_ctx,
                .technique = technique,
            };
            u32 pass_start = (u32)plan->generated_pass_names.count;

            Mel_Technique_Compile_Result result = technique->compile(&compile_ctx);
            if (result == MEL_TECHNIQUE_COMPILE_FAIL)
                goto fail;
            if (result == MEL_TECHNIQUE_COMPILE_CONTRIBUTED)
            {
                u32 snapshot_start = (u32)plan->source_snapshots.count;
                u32 source_count = mel_view_source_count(binding.view);
                for (u32 source_index = 0; source_index < source_count; source_index++)
                {
                    Mel_Source_Handle source = mel_view_source_at(binding.view, source_index);
                    mel_array_push(&plan->source_snapshots, ((Mel_Frame_Plan_Source_Snapshot){
                        .source = source,
                        .shape_version = mel_source_shape_version(source),
                    }));
                }

                mel_array_push(&plan->resolved_techniques, ((Mel_Frame_Plan_Resolved_Record){
                    .public_info = {
                        .view = binding.view,
                        .family = family,
                        .technique_name = str8_dup(technique->name, plan->alloc),
                        .binding_index = i,
                    },
                    .pass_start = pass_start,
                    .pass_count = (u32)plan->generated_pass_names.count - pass_start,
                    .source_snapshot_start = snapshot_start,
                    .source_snapshot_count = source_count,
                    .parameter_version = mel_view_parameter_version(binding.view),
                    .topology_version = mel_view_topology_version(binding.view),
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

bool mel_frame_plan_refresh_opt(Mel_Frame_Plan_Handle handle, Mel_Frame_Plan_Refresh_Opt opt)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    Mel_Gpu_Device* dev = opt.dev ? opt.dev : (plan->dev ? plan->dev : mel_gpu_dev());
    plan->dirty_flags = MEL_FRAME_DIRTY_NONE;

    for (usize i = 0; i < plan->generated_targets.count; i++)
    {
        if (!mel__frame_plan_refresh_target(plan, &plan->generated_targets.items[i], dev))
            return false;
    }

    for (usize i = 0; i < plan->resolved_techniques.count; i++)
    {
        Mel_Frame_Plan_Resolved_Record* record = &plan->resolved_techniques.items[i];
        if (mel_view_topology_version(record->public_info.view) != record->topology_version)
        {
            plan->dirty_flags |= MEL_FRAME_DIRTY_TOPOLOGY;
            return false;
        }

        for (u32 source_index = 0; source_index < record->source_snapshot_count; source_index++)
        {
            Mel_Frame_Plan_Source_Snapshot* snapshot =
                &plan->source_snapshots.items[record->source_snapshot_start + source_index];
            if (mel_source_shape_version(snapshot->source) != snapshot->shape_version)
            {
                plan->dirty_flags |= MEL_FRAME_DIRTY_SOURCE_SHAPE;
                return false;
            }
        }

        u64 parameter_version = mel_view_parameter_version(record->public_info.view);
        if (parameter_version == record->parameter_version)
            continue;

        const Mel_Technique_Desc* technique = mel_render_technique_find(record->public_info.family,
            record->public_info.technique_name);
        if (!technique || !technique->refresh)
            return false;

        str8* pass_names = record->pass_count
            ? &plan->generated_pass_names.items[record->pass_start]
            : nullptr;
        Mel_Technique_Refresh_Ctx refresh_ctx = {
            .plan = handle,
            .graph = plan->graph,
            .view = record->public_info.view,
            .technique = technique,
            .pass_names = pass_names,
            .pass_count = record->pass_count,
        };
        if (!technique->refresh(&refresh_ctx))
            return false;

        record->parameter_version = parameter_version;
        plan->dirty_flags |= MEL_FRAME_DIRTY_PARAMETERS;
    }

    return true;
}

u32 mel_frame_plan_dirty_flags(Mel_Frame_Plan_Handle handle)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    return plan->dirty_flags;
}

Mel_Render_Target* mel_frame_plan_swapchain_target(Mel_Frame_Plan_Handle handle, Mel_Swapchain_Handle swapchain)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    Mel_Gpu_Device* dev = plan->dev ? plan->dev : mel_gpu_dev();
    Mel_Frame_Plan_Target* target = mel__frame_plan_get_target(plan, swapchain, dev, MEL_RENDER_TARGET_SWAPCHAIN);
    return target ? &target->target : nullptr;
}

Mel_Render_Target* mel_frame_plan_swapchain_depth_target(Mel_Frame_Plan_Handle handle, Mel_Swapchain_Handle swapchain)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    Mel_Gpu_Device* dev = plan->dev ? plan->dev : mel_gpu_dev();
    Mel_Frame_Plan_Target* target = mel__frame_plan_get_target(plan, swapchain, dev, MEL_RENDER_TARGET_DEPTH);
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
        *out = plan->resolved_techniques.items[index].public_info;
    return true;
}

u32 mel_frame_plan_technique_diagnostic_count(Mel_Frame_Plan_Handle handle)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    return (u32)plan->diagnostics.count;
}

bool mel_frame_plan_technique_diagnostic_at(Mel_Frame_Plan_Handle handle, u32 index, Mel_Frame_Plan_Technique_Diagnostic* out)
{
    Mel_Frame_Plan* plan = mel__frame_plan_get(handle);
    if (index >= (u32)plan->diagnostics.count)
        return false;
    if (out)
        *out = plan->diagnostics.items[index].public_info;
    return true;
}
