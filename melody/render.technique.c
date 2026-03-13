#include "render.technique.h"
#include "render.frame_plan.h"
#include "render.graph.h"
#include "render.view.h"
#include "render.source.h"
#include "render.list.h"
#include "mesh.pass.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "core.engine.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

typedef struct {
    Mel_Technique_Desc desc;
} Mel__Technique_Registry_Entry;

typedef struct {
    Mel_Technique_Family_Policy policy;
} Mel__Technique_Policy_Entry;

static Mel_Array(Mel__Technique_Registry_Entry) s_registry;
static Mel_Array(Mel__Technique_Policy_Entry) s_policies;
static bool s_initialized;

static Mel_Technique_Check_Result mel__support_always(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    MEL_UNUSED(ctx);
    return (Mel_Technique_Check_Result){
        .ok = true,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8("supported by current device"),
    };
}

static Mel_Technique_Check_Result mel__match_always(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    MEL_UNUSED(ctx);
    return (Mel_Technique_Check_Result){
        .ok = true,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8("always available"),
    };
}

static Mel_Technique_Policy_Result mel__policy_allow_default(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(technique);
    MEL_UNUSED(user);
    return (Mel_Technique_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8(""),
    };
}

static bool mel__refresh_view_passes(const Mel_Technique_Refresh_Ctx* ctx)
{
    return mel_frame_plan_refresh_contributed_passes(ctx->plan, ctx->view,
        ctx->pass_names, ctx->pass_count);
}

static Mel_Source_Handle* mel__merge_mesh_gpu_sources(Mel_Frame_Plan_Handle plan,
    Mel_Source_Handle* geometry_sources, Mel_Source_Handle* material_sources)
{
    if (!geometry_sources && !material_sources)
        return nullptr;

    u32 geometry_count = 0;
    if (geometry_sources)
        while (mel_source_handle_valid(geometry_sources[geometry_count]))
            geometry_count++;

    u32 material_count = 0;
    if (material_sources)
        while (mel_source_handle_valid(material_sources[material_count]))
            material_count++;

    Mel_Source_Handle* merged = mel_alloc(mel_alloc_heap(),
        sizeof(Mel_Source_Handle) * (geometry_count + material_count + 1));
    u32 out = 0;
    for (u32 i = 0; i < geometry_count; i++)
        merged[out++] = geometry_sources[i];
    for (u32 i = 0; i < material_count; i++)
        merged[out++] = material_sources[i];
    merged[out] = MEL_SOURCE_HANDLE_NULL;
    return merged;
}

static Mel_Source_Handle* mel__collect_preferred_mesh_geometry_sources(Mel_Frame_Plan_Handle plan,
    Mel_View_Handle view, u32* out_schema)
{
    Mel_Source_Handle* sources = mel_frame_plan_collect_sources(plan, view, MEL_SOURCE_GPU_BUFFER,
        MEL_SCHEMA_MESH_CULL_BATCH_STREAM);
    if (sources)
    {
        if (out_schema) *out_schema = MEL_SCHEMA_MESH_CULL_BATCH_STREAM;
        return sources;
    }

    sources = mel_frame_plan_collect_sources(plan, view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_CULL_STREAM);
    if (sources)
    {
        if (out_schema) *out_schema = MEL_SCHEMA_MESH_CULL_STREAM;
        return sources;
    }

    sources = mel_frame_plan_collect_sources(plan, view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_INDIRECT_STREAM);
    if (sources)
    {
        if (out_schema) *out_schema = MEL_SCHEMA_MESH_INDIRECT_STREAM;
        return sources;
    }

    sources = mel_frame_plan_collect_sources(plan, view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    if (sources)
    {
        if (out_schema) *out_schema = MEL_SCHEMA_MESH_DRAW_STREAM;
        return sources;
    }

    if (out_schema) *out_schema = MEL_SCHEMA_INVALID;
    return nullptr;
}

static bool mel__material_record_visibility_compatible(const Mel_Material_Gpu_Record* record)
{
    if (!record)
        return true;
    return (u32)record->params1.x == MEL_MATERIAL_DOMAIN_OPAQUE;
}

static bool mel__mesh_entry_visibility_compatible(const Mel_Mesh_Entry* entry)
{
    if (!entry || !mel_material_instance_handle_valid(entry->material))
        return true;
    return mel_material_template_render_domain(mel_material_instance_template(entry->material)) == MEL_MATERIAL_DOMAIN_OPAQUE;
}

static bool mel__mesh_lists_visibility_compatible(Mel_Render_List** lists)
{
    if (!lists)
        return true;

    for (u32 list_index = 0; lists[list_index] != nullptr; list_index++)
    {
        Mel_Render_List* list = lists[list_index];
        for (u32 i = 0; i < list->count; i++)
        {
            Mel_Mesh_Entry* entry = mel_render_list_get(list, list->packets[i].entry_index);
            if (!mel__mesh_entry_visibility_compatible(entry))
                return false;
        }
    }

    return true;
}

static bool mel__material_tables_visibility_compatible(Mel_Source_Handle* sources)
{
    if (!sources)
        return true;

    for (u32 i = 0; mel_source_handle_valid(sources[i]); i++)
    {
        Mel_Material_Table* table = mel_source_user(sources[i]);
        if (!table)
            continue;
        for (u32 record_index = 0; record_index < mel_material_table_count(table); record_index++)
        {
            if (!mel__material_record_visibility_compatible(&mel_material_table_records(table)[record_index]))
                return false;
        }
    }

    return true;
}

static Mel_Technique_Check_Result mel__support_mesh_shader(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Gpu_Capabilities caps = ctx->dev ? mel_gpu_capabilities(ctx->dev) : (Mel_Gpu_Capabilities){0};
    bool ok = caps.mesh_shader;
    return (Mel_Technique_Check_Result){
        .ok = ok,
        .kind = ok ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE,
        .reason = ok ? S8("mesh shader capability available") : S8("mesh shader capability unavailable"),
    };
}

static Mel_Technique_Compile_Result mel__compile_sprite(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Sprite_Pass* sprite_pass = ctx->plan_ctx->opt.sprite_pass ? ctx->plan_ctx->opt.sprite_pass : mel_sprite_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_SPRITE);
    if (!read_lists)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_render_list_pass(ctx->plan_ctx, ctx->technique->name,
        mel_sprite_pass_execute, sprite_pass, read_lists);
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Check_Result mel__match_sprite(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_SPRITE);
    bool matched = read_lists != nullptr;
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found sprite render-list source") : S8("no sprite render-list source attached"),
    };
}

static Mel_Technique_Compile_Result mel__compile_text(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Text_Pass* text_pass = ctx->plan_ctx->opt.text_pass ? ctx->plan_ctx->opt.text_pass : mel_text_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_TEXT);
    if (!read_lists)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_render_list_pass(ctx->plan_ctx, ctx->technique->name,
        mel_text_pass_execute, text_pass, read_lists);
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Check_Result mel__match_text(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_TEXT);
    bool matched = read_lists != nullptr;
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found text render-list source") : S8("no text render-list source attached"),
    };
}

static Mel_Technique_Compile_Result mel__compile_mesh(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    bool wants_list_path = ctx->technique->source_schema == MEL_SCHEMA_MESH_INSTANCE;
    bool wants_draw_stream_path = ctx->technique->source_schema == MEL_SCHEMA_MESH_DRAW_STREAM;
    bool wants_indirect_path = ctx->technique->source_schema == MEL_SCHEMA_MESH_INDIRECT_STREAM;
    u32 gpu_schema = wants_indirect_path ? MEL_SCHEMA_MESH_INDIRECT_STREAM : MEL_SCHEMA_MESH_DRAW_STREAM;

    Mel_Render_List** read_lists = wants_list_path
        ? mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE)
        : nullptr;
    Mel_Source_Handle* draw_sources = (wants_draw_stream_path || wants_indirect_path)
        ? mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, gpu_schema)
        : nullptr;
    Mel_Source_Handle* material_sources = (wants_draw_stream_path || wants_indirect_path)
        ? mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE)
        : nullptr;
    Mel_Source_Handle* read_sources = mel__merge_mesh_gpu_sources(ctx->plan_ctx->plan, draw_sources, material_sources);

    if (!read_lists && !read_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, draw_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        return MEL_TECHNIQUE_COMPILE_SKIP;
    }

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);
    bool ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute, mesh_pass, read_lists, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, draw_sources);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
    if (read_sources)
        mel_dealloc(mel_alloc_heap(), read_sources);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Compile_Result mel__compile_mesh_compute_indirect(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    Mel_Source_Handle* cull_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_CULL_STREAM);
    Mel_Source_Handle* material_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE);
    Mel_Source_Handle* read_sources = mel__merge_mesh_gpu_sources(ctx->plan_ctx->plan, cull_sources, material_sources);
    if (!read_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, cull_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        return MEL_TECHNIQUE_COMPILE_SKIP;
    }

    str8 compute_name = str8_fmt(mel_alloc_heap(), "%.*s.compute", (int)ctx->technique->name.len, ctx->technique->name.data);
    bool ok = mel_frame_plan_add_compute_pass(ctx->plan_ctx, compute_name, mel_mesh_pass_execute_compute_indirect,
        mesh_pass, read_sources, nullptr);
    mel_dealloc(mel_alloc_heap(), compute_name.data);
    if (!ok)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, cull_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        mel_dealloc(mel_alloc_heap(), read_sources);
        return MEL_TECHNIQUE_COMPILE_FAIL;
    }

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute, mesh_pass, nullptr, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, cull_sources);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
    mel_dealloc(mel_alloc_heap(), read_sources);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Compile_Result mel__compile_mesh_compute_indirect_batch(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    Mel_Source_Handle* cull_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_CULL_BATCH_STREAM);
    Mel_Source_Handle* material_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE);
    Mel_Source_Handle* read_sources = mel__merge_mesh_gpu_sources(ctx->plan_ctx->plan, cull_sources, material_sources);
    if (!read_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, cull_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        return MEL_TECHNIQUE_COMPILE_SKIP;
    }

    str8 compute_name = str8_fmt(mel_alloc_heap(), "%.*s.compute", (int)ctx->technique->name.len, ctx->technique->name.data);
    bool ok = mel_frame_plan_add_compute_pass(ctx->plan_ctx, compute_name, mel_mesh_pass_execute_compute_indirect,
        mesh_pass, read_sources, nullptr);
    mel_dealloc(mel_alloc_heap(), compute_name.data);
    if (!ok)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, cull_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        mel_dealloc(mel_alloc_heap(), read_sources);
        return MEL_TECHNIQUE_COMPILE_FAIL;
    }

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute, mesh_pass, nullptr, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, cull_sources);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
    mel_dealloc(mel_alloc_heap(), read_sources);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Compile_Result mel__compile_mesh_shader(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    Mel_Source_Handle* draw_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    Mel_Source_Handle* material_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE);
    Mel_Source_Handle* read_sources = mel__merge_mesh_gpu_sources(ctx->plan_ctx->plan, draw_sources, material_sources);
    if (!read_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, draw_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        return MEL_TECHNIQUE_COMPILE_SKIP;
    }

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);
    bool ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute_mesh_shader, mesh_pass, nullptr, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, draw_sources);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
    mel_dealloc(mel_alloc_heap(), read_sources);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Compile_Result mel__compile_mesh_visibility_buffer(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    u32 selected_schema = MEL_SCHEMA_INVALID;
    Mel_Source_Handle* geometry_sources = mel__collect_preferred_mesh_geometry_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, &selected_schema);
    Mel_Source_Handle* material_sources = geometry_sources
        ? mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE)
        : nullptr;
    Mel_Source_Handle* read_sources = mel__merge_mesh_gpu_sources(ctx->plan_ctx->plan, geometry_sources, material_sources);
    Mel_Render_List** read_lists = geometry_sources ? nullptr
        : mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);

    if (!read_lists && !read_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        return MEL_TECHNIQUE_COMPILE_SKIP;
    }

    str8 visibility_name = str8_fmt(mel_alloc_heap(), "mesh_visibility_%u", ctx->plan_ctx->binding_index);
    Mel_Render_Target* visibility_target = mel_frame_plan_named_color_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain, visibility_name, VK_FORMAT_R32G32B32A32_SFLOAT);
    str8 attribute_name = str8_fmt(mel_alloc_heap(), "mesh_visibility_attr_%u", ctx->plan_ctx->binding_index);
    Mel_Render_Target* attribute_target = mel_frame_plan_named_color_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain, attribute_name, VK_FORMAT_R32G32B32A32_SFLOAT);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);

    bool ok = true;
    if (selected_schema == MEL_SCHEMA_MESH_CULL_STREAM || selected_schema == MEL_SCHEMA_MESH_CULL_BATCH_STREAM)
    {
        str8 compute_name = str8_fmt(mel_alloc_heap(), "%.*s.compute",
            (int)ctx->technique->name.len, ctx->technique->name.data);
        ok = mel_frame_plan_add_compute_pass(ctx->plan_ctx, compute_name,
            mel_mesh_pass_execute_compute_indirect, mesh_pass, read_sources, nullptr);
        mel_dealloc(mel_alloc_heap(), compute_name.data);
        if (!ok)
        {
            mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
            mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
            mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
            if (read_sources)
                mel_dealloc(mel_alloc_heap(), read_sources);
            mel_dealloc(mel_alloc_heap(), visibility_name.data);
            mel_dealloc(mel_alloc_heap(), attribute_name.data);
            return MEL_TECHNIQUE_COMPILE_FAIL;
        }
    }

    str8 fill_suffix = str8_fmt(mel_alloc_heap(), "%.*s.fill", (int)ctx->technique->name.len, ctx->technique->name.data);
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, fill_suffix,
        mel_mesh_pass_execute_visibility_fill, mesh_pass, read_lists, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = visibility_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.5f, .g = 0.5f, .b = 1.0f, .a = 0.0f } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_dealloc(mel_alloc_heap(), fill_suffix.data);
    if (!ok)
    {
        mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        if (read_sources)
            mel_dealloc(mel_alloc_heap(), read_sources);
        mel_dealloc(mel_alloc_heap(), visibility_name.data);
        mel_dealloc(mel_alloc_heap(), attribute_name.data);
        return MEL_TECHNIQUE_COMPILE_FAIL;
    }

    str8 attr_suffix = str8_fmt(mel_alloc_heap(), "%.*s.attributes", (int)ctx->technique->name.len, ctx->technique->name.data);
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, attr_suffix,
        mel_mesh_pass_execute_visibility_attribute_fill, mesh_pass, read_lists, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = attribute_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_dealloc(mel_alloc_heap(), attr_suffix.data);
    if (!ok)
    {
        mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        if (read_sources)
            mel_dealloc(mel_alloc_heap(), read_sources);
        mel_dealloc(mel_alloc_heap(), visibility_name.data);
        mel_dealloc(mel_alloc_heap(), attribute_name.data);
        return MEL_TECHNIQUE_COMPILE_FAIL;
    }

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* resolve_read_targets[] = { visibility_target, attribute_target, nullptr };
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute_visibility_resolve, mesh_pass, nullptr, material_sources, resolve_read_targets,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } }));

    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
    if (read_sources)
        mel_dealloc(mel_alloc_heap(), read_sources);
    mel_dealloc(mel_alloc_heap(), visibility_name.data);
    mel_dealloc(mel_alloc_heap(), attribute_name.data);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Compile_Result mel__compile_mesh_deferred(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    u32 selected_schema = MEL_SCHEMA_INVALID;
    Mel_Source_Handle* geometry_sources = mel__collect_preferred_mesh_geometry_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, &selected_schema);
    Mel_Source_Handle* material_sources = geometry_sources
        ? mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE)
        : nullptr;
    Mel_Source_Handle* read_sources = mel__merge_mesh_gpu_sources(ctx->plan_ctx->plan, geometry_sources, material_sources);
    Mel_Render_List** read_lists = geometry_sources ? nullptr
        : mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
            ctx->plan_ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);

    if (!read_lists && !read_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        return MEL_TECHNIQUE_COMPILE_SKIP;
    }

    str8 deferred_normal_name = str8_fmt(mel_alloc_heap(), "mesh_deferred_normal_%u", ctx->plan_ctx->binding_index);
    Mel_Render_Target* deferred_normal_target = mel_frame_plan_named_color_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain, deferred_normal_name, VK_FORMAT_R32G32B32A32_SFLOAT);
    str8 deferred_albedo_name = str8_fmt(mel_alloc_heap(), "mesh_deferred_albedo_%u", ctx->plan_ctx->binding_index);
    Mel_Render_Target* deferred_albedo_target = mel_frame_plan_named_color_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain, deferred_albedo_name, VK_FORMAT_R32G32B32A32_SFLOAT);
    str8 deferred_emissive_name = str8_fmt(mel_alloc_heap(), "mesh_deferred_emissive_%u", ctx->plan_ctx->binding_index);
    Mel_Render_Target* deferred_emissive_target = mel_frame_plan_named_color_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain, deferred_emissive_name, VK_FORMAT_R32G32B32A32_SFLOAT);
    str8 deferred_meta_name = str8_fmt(mel_alloc_heap(), "mesh_deferred_meta_%u", ctx->plan_ctx->binding_index);
    Mel_Render_Target* deferred_meta_target = mel_frame_plan_named_color_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain, deferred_meta_name, VK_FORMAT_R32G32B32A32_SFLOAT);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);

    bool ok = true;
    if (selected_schema == MEL_SCHEMA_MESH_CULL_STREAM || selected_schema == MEL_SCHEMA_MESH_CULL_BATCH_STREAM)
    {
        str8 compute_name = str8_fmt(mel_alloc_heap(), "%.*s.compute",
            (int)ctx->technique->name.len, ctx->technique->name.data);
        ok = mel_frame_plan_add_compute_pass(ctx->plan_ctx, compute_name,
            mel_mesh_pass_execute_compute_indirect, mesh_pass, read_sources, nullptr);
        mel_dealloc(mel_alloc_heap(), compute_name.data);
        if (!ok)
        {
            mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
            mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
            mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
            if (read_sources)
                mel_dealloc(mel_alloc_heap(), read_sources);
            mel_dealloc(mel_alloc_heap(), deferred_normal_name.data);
            mel_dealloc(mel_alloc_heap(), deferred_albedo_name.data);
            mel_dealloc(mel_alloc_heap(), deferred_emissive_name.data);
            mel_dealloc(mel_alloc_heap(), deferred_meta_name.data);
            return MEL_TECHNIQUE_COMPILE_FAIL;
        }
    }

    str8 fill_suffix = str8_fmt(mel_alloc_heap(), "%.*s.fill", (int)ctx->technique->name.len, ctx->technique->name.data);
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, fill_suffix,
        mel_mesh_pass_execute_deferred_fill, mesh_pass, read_lists, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = deferred_normal_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.5f, .g = 0.5f, .b = 1.0f, .a = 1.0f } },
            { .target = deferred_albedo_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f } },
            { .target = deferred_emissive_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f } },
            { .target = deferred_meta_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_dealloc(mel_alloc_heap(), fill_suffix.data);
    if (!ok)
    {
        mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
        mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
        if (read_sources)
            mel_dealloc(mel_alloc_heap(), read_sources);
        mel_dealloc(mel_alloc_heap(), deferred_normal_name.data);
        mel_dealloc(mel_alloc_heap(), deferred_albedo_name.data);
        mel_dealloc(mel_alloc_heap(), deferred_emissive_name.data);
        mel_dealloc(mel_alloc_heap(), deferred_meta_name.data);
        return MEL_TECHNIQUE_COMPILE_FAIL;
    }

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* resolve_read_targets[] = {
        deferred_normal_target,
        deferred_albedo_target,
        deferred_emissive_target,
        deferred_meta_target,
        nullptr
    };
    ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute_deferred_resolve, mesh_pass, nullptr, nullptr, resolve_read_targets,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } }));

    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, geometry_sources);
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, material_sources);
    if (read_sources)
        mel_dealloc(mel_alloc_heap(), read_sources);
    mel_dealloc(mel_alloc_heap(), deferred_normal_name.data);
    mel_dealloc(mel_alloc_heap(), deferred_albedo_name.data);
    mel_dealloc(mel_alloc_heap(), deferred_emissive_name.data);
    mel_dealloc(mel_alloc_heap(), deferred_meta_name.data);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Check_Result mel__match_mesh_forward(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);
    bool matched = read_lists != nullptr;
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found mesh-instance render-list source") : S8("no mesh-instance render-list source attached"),
    };
}

static Mel_Technique_Check_Result mel__match_mesh_draw_stream(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan,
        ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    bool matched = read_sources != nullptr;
    mel_frame_plan_free_read_sources(ctx->plan, read_sources);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found gpu draw-stream source") : S8("no gpu draw-stream source attached"),
    };
}

static Mel_Technique_Check_Result mel__match_mesh_indirect_stream(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan,
        ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_INDIRECT_STREAM);
    bool matched = read_sources != nullptr;
    mel_frame_plan_free_read_sources(ctx->plan, read_sources);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found gpu indirect stream source") : S8("no gpu indirect stream source attached"),
    };
}

static Mel_Technique_Check_Result mel__match_mesh_cull_stream(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan,
        ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_CULL_STREAM);
    bool matched = read_sources != nullptr;
    mel_frame_plan_free_read_sources(ctx->plan, read_sources);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found gpu compute-cull stream source") : S8("no gpu compute-cull stream source attached"),
    };
}

static Mel_Technique_Check_Result mel__match_mesh_cull_batch_stream(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan,
        ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_CULL_BATCH_STREAM);
    bool matched = read_sources != nullptr;
    mel_frame_plan_free_read_sources(ctx->plan, read_sources);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found gpu batched compute-cull stream source") : S8("no gpu batched compute-cull stream source attached"),
    };
}

static Mel_Technique_Check_Result mel__match_mesh_visibility_buffer(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* gpu_sources = mel__collect_preferred_mesh_geometry_sources(ctx->plan, ctx->binding.view, nullptr);
    Mel_Source_Handle* material_sources = gpu_sources
        ? mel_frame_plan_collect_sources(ctx->plan, ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE)
        : nullptr;
    Mel_Render_List** read_lists = gpu_sources ? nullptr
        : mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);
    bool matched = gpu_sources != nullptr || read_lists != nullptr;

    if (matched && gpu_sources && !material_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
        mel_frame_plan_free_read_sources(ctx->plan, material_sources);
        mel_frame_plan_free_read_lists(ctx->plan, read_lists);
        return (Mel_Technique_Check_Result){
            .ok = false,
            .kind = MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
            .reason = S8("visibility-buffer gpu paths require a material-table source"),
        };
    }

    if (matched && read_lists && !mel__mesh_lists_visibility_compatible(read_lists))
    {
        mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
        mel_frame_plan_free_read_sources(ctx->plan, material_sources);
        mel_frame_plan_free_read_lists(ctx->plan, read_lists);
        return (Mel_Technique_Check_Result){
            .ok = false,
            .kind = MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
            .reason = S8("visibility-buffer currently supports opaque mesh materials only"),
        };
    }

    if (matched && material_sources && !mel__material_tables_visibility_compatible(material_sources))
    {
        mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
        mel_frame_plan_free_read_sources(ctx->plan, material_sources);
        mel_frame_plan_free_read_lists(ctx->plan, read_lists);
        return (Mel_Technique_Check_Result){
            .ok = false,
            .kind = MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
            .reason = S8("visibility-buffer currently supports opaque material-table records only"),
        };
    }

    mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
    mel_frame_plan_free_read_sources(ctx->plan, material_sources);
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched
            ? S8("found mesh sources for visibility-buffer shading")
            : S8("no mesh sources attached for visibility-buffer shading"),
    };
}

static Mel_Technique_Check_Result mel__match_mesh_deferred(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* gpu_sources = mel__collect_preferred_mesh_geometry_sources(ctx->plan, ctx->binding.view, nullptr);
    Mel_Source_Handle* material_sources = gpu_sources
        ? mel_frame_plan_collect_sources(ctx->plan, ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MATERIAL_TABLE)
        : nullptr;
    Mel_Render_List** read_lists = gpu_sources ? nullptr
        : mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);
    bool matched = gpu_sources != nullptr || read_lists != nullptr;

    if (matched && gpu_sources && !material_sources)
    {
        mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
        mel_frame_plan_free_read_sources(ctx->plan, material_sources);
        mel_frame_plan_free_read_lists(ctx->plan, read_lists);
        return (Mel_Technique_Check_Result){
            .ok = false,
            .kind = MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
            .reason = S8("deferred gpu paths require a material-table source"),
        };
    }

    if (matched && read_lists && !mel__mesh_lists_visibility_compatible(read_lists))
    {
        mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
        mel_frame_plan_free_read_sources(ctx->plan, material_sources);
        mel_frame_plan_free_read_lists(ctx->plan, read_lists);
        return (Mel_Technique_Check_Result){
            .ok = false,
            .kind = MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
            .reason = S8("deferred currently supports opaque mesh materials only"),
        };
    }

    if (matched && material_sources && !mel__material_tables_visibility_compatible(material_sources))
    {
        mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
        mel_frame_plan_free_read_sources(ctx->plan, material_sources);
        mel_frame_plan_free_read_lists(ctx->plan, read_lists);
        return (Mel_Technique_Check_Result){
            .ok = false,
            .kind = MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
            .reason = S8("deferred currently supports opaque material-table records only"),
        };
    }

    mel_frame_plan_free_read_sources(ctx->plan, gpu_sources);
    mel_frame_plan_free_read_sources(ctx->plan, material_sources);
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched
            ? S8("found mesh sources for deferred shading")
            : S8("no mesh sources attached for deferred shading"),
    };
}

static Mel_Technique_Compile_Result mel__compile_debug(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Sprite_Pass* sprite_pass = ctx->plan_ctx->opt.sprite_pass ? ctx->plan_ctx->opt.sprite_pass : mel_sprite_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_SPRITE);
    if (!read_lists)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_render_list_pass(ctx->plan_ctx, ctx->technique->name,
        mel_sprite_pass_execute, sprite_pass, read_lists);
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Check_Result mel__match_debug(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_SPRITE);
    bool matched = read_lists != nullptr;
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found debug sprite render-list source") : S8("no debug sprite render-list source attached"),
    };
}

static Mel_Technique_Compile_Result mel__compile_ui(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Sprite_Pass* sprite_pass = ctx->plan_ctx->opt.sprite_pass ? ctx->plan_ctx->opt.sprite_pass : mel_sprite_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_SPRITE);
    if (!read_lists)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_render_list_pass(ctx->plan_ctx, ctx->technique->name,
        mel_sprite_pass_execute, sprite_pass, read_lists);
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Check_Result mel__match_ui(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan, ctx->binding.view, MEL_SCHEMA_SPRITE);
    bool matched = read_lists != nullptr;
    mel_frame_plan_free_read_lists(ctx->plan, read_lists);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched ? S8("found ui sprite render-list source") : S8("no ui sprite render-list source attached"),
    };
}

static void mel__imgui_execute(Mel_Render_Pass_Ctx* ctx)
{
    Mel_ImGui_Draw_Callback* callback = ctx->user;
    if (callback && callback->fn)
        callback->fn(callback->user);

    igRender();
    ImDrawData* draw_data = igGetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0)
        ImGui_ImplVulkan_RenderDrawData(draw_data, ctx->cmd.cmd, VK_NULL_HANDLE);
}

static Mel_Technique_Compile_Result mel__compile_imgui(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_ImGui_Draw_Callback* callback = mel_view_user(ctx->plan_ctx->binding.view);
    if (!callback)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_pass(ctx->plan_ctx, ctx->technique->name,
        mel__imgui_execute, callback, nullptr, nullptr, nullptr);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Check_Result mel__match_imgui(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_ImGui_Draw_Callback* callback = mel_view_user(ctx->binding.view);
    bool matched = callback != nullptr;
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_VIEW_MISMATCH,
        .reason = matched
            ? (callback->fn ? S8("view has imgui callback") : S8("view has imgui callback slot"))
            : S8("view has no imgui callback"),
    };
}

static bool mel__refresh_imgui(const Mel_Technique_Refresh_Ctx* ctx)
{
    if (!mel__refresh_view_passes(ctx))
        return false;

    for (u32 i = 0; i < ctx->pass_count; i++)
    {
        for (usize pass_index = 0; pass_index < ctx->graph->passes.count; pass_index++)
        {
            Mel_Render_Graph_Pass* pass = &ctx->graph->passes.items[pass_index];
            if (!str8_equals(pass->name, ctx->pass_names[i]))
                continue;
            pass->user = mel_view_user(ctx->view);
            break;
        }
    }

    return true;
}

__attribute__((constructor(214)))
static void mel__render_technique_registry_init(void)
{
    mel_array_init(&s_registry, mel_alloc_heap());
    mel_array_init(&s_policies, mel_alloc_heap());
    s_initialized = true;

    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_SPRITE,
        .name = S8("sprite"),
        .source_schema = MEL_SCHEMA_SPRITE,
        .priority = 100,
        .supports = mel__support_always,
        .matches = mel__match_sprite,
        .compile = mel__compile_sprite,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_TEXT,
        .name = S8("text"),
        .source_schema = MEL_SCHEMA_TEXT,
        .priority = 100,
        .supports = mel__support_always,
        .matches = mel__match_text,
        .compile = mel__compile_text,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.mesh_shader"),
        .source_schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .priority = 350,
        .supports = mel__support_mesh_shader,
        .matches = mel__match_mesh_draw_stream,
        .compile = mel__compile_mesh_shader,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.compute_indirect"),
        .source_schema = MEL_SCHEMA_MESH_CULL_STREAM,
        .priority = 300,
        .supports = mel__support_always,
        .matches = mel__match_mesh_cull_stream,
        .compile = mel__compile_mesh_compute_indirect,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.compute_indirect_batch"),
        .source_schema = MEL_SCHEMA_MESH_CULL_BATCH_STREAM,
        .priority = 320,
        .supports = mel__support_always,
        .matches = mel__match_mesh_cull_batch_stream,
        .compile = mel__compile_mesh_compute_indirect_batch,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.indirect"),
        .source_schema = MEL_SCHEMA_MESH_INDIRECT_STREAM,
        .priority = 250,
        .supports = mel__support_always,
        .matches = mel__match_mesh_indirect_stream,
        .compile = mel__compile_mesh,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.visibility_buffer"),
        .source_schema = MEL_SCHEMA_INVALID,
        .priority = 80,
        .supports = mel__support_always,
        .matches = mel__match_mesh_visibility_buffer,
        .compile = mel__compile_mesh_visibility_buffer,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.deferred"),
        .source_schema = MEL_SCHEMA_INVALID,
        .priority = 70,
        .supports = mel__support_always,
        .matches = mel__match_mesh_deferred,
        .compile = mel__compile_mesh_deferred,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.draw_stream"),
        .source_schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .priority = 200,
        .supports = mel__support_always,
        .matches = mel__match_mesh_draw_stream,
        .compile = mel__compile_mesh,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh.forward"),
        .source_schema = MEL_SCHEMA_MESH_INSTANCE,
        .priority = 100,
        .supports = mel__support_always,
        .matches = mel__match_mesh_forward,
        .compile = mel__compile_mesh,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_DEBUG,
        .name = S8("debug"),
        .source_schema = MEL_SCHEMA_SPRITE,
        .priority = 100,
        .supports = mel__support_always,
        .matches = mel__match_debug,
        .compile = mel__compile_debug,
        .refresh = mel__refresh_view_passes,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_IMGUI,
        .name = S8("imgui"),
        .source_schema = 0,
        .priority = 100,
        .supports = mel__support_always,
        .matches = mel__match_imgui,
        .compile = mel__compile_imgui,
        .refresh = mel__refresh_imgui,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_UI,
        .name = S8("ui"),
        .source_schema = MEL_SCHEMA_SPRITE,
        .priority = 100,
        .supports = mel__support_always,
        .matches = mel__match_ui,
        .compile = mel__compile_ui,
        .refresh = mel__refresh_view_passes,
    });
}

__attribute__((destructor(214)))
static void mel__render_technique_registry_shutdown(void)
{
    if (!s_initialized)
        return;

    for (usize i = 0; i < s_registry.count; i++)
        mel_dealloc(mel_alloc_heap(), s_registry.items[i].desc.name.data);
    mel_array_free(&s_registry);
    mel_array_free(&s_policies);
    s_initialized = false;
}

void mel_render_technique_register(const Mel_Technique_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);
    assert(desc->family != MEL_TECHNIQUE_NONE);
    assert(desc->compile != nullptr);

    for (usize i = 0; i < s_registry.count; i++)
    {
        if (s_registry.items[i].desc.family == desc->family &&
            str8_equals(s_registry.items[i].desc.name, desc->name))
        {
            mel_dealloc(mel_alloc_heap(), s_registry.items[i].desc.name.data);
            s_registry.items[i].desc = *desc;
            s_registry.items[i].desc.name = str8_dup(desc->name, mel_alloc_heap());
            return;
        }
    }

    Mel__Technique_Registry_Entry entry = {
        .desc = *desc,
    };
    entry.desc.name = str8_dup(desc->name, mel_alloc_heap());
    mel_array_push(&s_registry, entry);
}

void mel_render_technique_unregister(Mel_Technique_Family_Id family, str8 name)
{
    assert(s_initialized);
    for (usize i = 0; i < s_registry.count; i++)
    {
        if (s_registry.items[i].desc.family == family &&
            str8_equals(s_registry.items[i].desc.name, name))
        {
            mel_dealloc(mel_alloc_heap(), s_registry.items[i].desc.name.data);
            mel_array_remove_ordered(&s_registry, i);
            return;
        }
    }
}

void mel_render_technique_set_family_policy(const Mel_Technique_Family_Policy* policy)
{
    assert(s_initialized);
    assert(policy != nullptr);
    assert(policy->family != MEL_TECHNIQUE_NONE);
    assert(policy->fn != nullptr);

    for (usize i = 0; i < s_policies.count; i++)
    {
        if (s_policies.items[i].policy.family == policy->family)
        {
            s_policies.items[i].policy = *policy;
            return;
        }
    }

    mel_array_push(&s_policies, ((Mel__Technique_Policy_Entry){
        .policy = *policy,
    }));
}

void mel_render_technique_clear_family_policy(Mel_Technique_Family_Id family)
{
    assert(s_initialized);

    for (usize i = 0; i < s_policies.count; i++)
    {
        if (s_policies.items[i].policy.family == family)
        {
            mel_array_remove_ordered(&s_policies, i);
            return;
        }
    }
}

const Mel_Technique_Desc* mel_render_technique_get(Mel_Technique_Family_Id family)
{
    assert(s_initialized);
    for (usize i = 0; i < s_registry.count; i++)
        if (s_registry.items[i].desc.family == family)
            return &s_registry.items[i].desc;
    return nullptr;
}

const Mel_Technique_Desc* mel_render_technique_find(Mel_Technique_Family_Id family, str8 name)
{
    assert(s_initialized);
    for (usize i = 0; i < s_registry.count; i++)
        if (s_registry.items[i].desc.family == family &&
            str8_equals(s_registry.items[i].desc.name, name))
            return &s_registry.items[i].desc;
    return nullptr;
}

u32 mel_render_technique_count_for_family(Mel_Technique_Family_Id family)
{
    assert(s_initialized);
    u32 count = 0;
    for (usize i = 0; i < s_registry.count; i++)
        if (s_registry.items[i].desc.family == family)
            count++;
    return count;
}

const Mel_Technique_Desc* mel_render_technique_at_for_family(Mel_Technique_Family_Id family, u32 index)
{
    assert(s_initialized);
    u32 count = 0;
    for (usize i = 0; i < s_registry.count; i++)
    {
        if (s_registry.items[i].desc.family != family)
            continue;
        if (count == index)
            return &s_registry.items[i].desc;
        count++;
    }
    return nullptr;
}

Mel_Technique_Policy_Result mel_render_technique_eval_family_policy(Mel_Technique_Family_Id family,
    Mel_Frame_Plan_Technique_Ctx* ctx, const Mel_Technique_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);

    for (usize i = 0; i < s_policies.count; i++)
    {
        if (s_policies.items[i].policy.family != family)
            continue;
        return s_policies.items[i].policy.fn
            ? s_policies.items[i].policy.fn(ctx, desc, s_policies.items[i].policy.user)
            : mel__policy_allow_default(ctx, desc, nullptr);
    }

    return mel__policy_allow_default(ctx, desc, nullptr);
}

Mel_Technique_Check_Result mel_render_technique_support(const Mel_Technique_Desc* desc, Mel_Frame_Plan_Technique_Ctx* ctx)
{
    assert(desc != nullptr);
    if (!desc->supports)
        return (Mel_Technique_Check_Result){ .ok = true, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("no support filter") };
    return desc->supports(ctx);
}

Mel_Technique_Check_Result mel_render_technique_match(const Mel_Technique_Desc* desc, Mel_Frame_Plan_Technique_Ctx* ctx)
{
    assert(desc != nullptr);
    if (!desc->matches)
        return (Mel_Technique_Check_Result){ .ok = true, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("no match filter") };
    return desc->matches(ctx);
}

const Mel_Technique_Desc* mel_render_technique_resolve(Mel_Technique_Family_Id family, Mel_Frame_Plan_Technique_Ctx* ctx)
{
    assert(s_initialized);
    const Mel_Technique_Desc* best = nullptr;
    i32 best_priority = 0;
    for (usize i = 0; i < s_registry.count; i++)
    {
        const Mel_Technique_Desc* desc = &s_registry.items[i].desc;
        if (desc->family != family)
            continue;
        Mel_Technique_Check_Result support = mel_render_technique_support(desc, ctx);
        if (!support.ok)
            continue;
        Mel_Technique_Check_Result match = mel_render_technique_match(desc, ctx);
        if (!match.ok)
            continue;
        Mel_Technique_Policy_Result policy = mel_render_technique_eval_family_policy(family, ctx, desc);
        if (!policy.allow)
            continue;
        i32 effective_priority = desc->priority + policy.priority_bias;
        if (!best || effective_priority > best_priority)
        {
            best = desc;
            best_priority = effective_priority;
        }
    }
    return best;
}

str8 mel_render_technique_name(Mel_Technique_Family_Id family)
{
    const Mel_Technique_Desc* desc = mel_render_technique_get(family);
    return desc ? desc->name : S8("");
}
