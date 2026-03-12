#include "render.technique.h"
#include "render.frame_plan.h"
#include "render.graph.h"
#include "render.view.h"
#include "render.source.h"
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

static Mel_Array(Mel__Technique_Registry_Entry) s_registry;
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

static bool mel__refresh_view_passes(const Mel_Technique_Refresh_Ctx* ctx)
{
    return mel_frame_plan_refresh_contributed_passes(ctx->plan, ctx->view,
        ctx->pass_names, ctx->pass_count);
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
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    if (!read_lists && !read_sources)
        return MEL_TECHNIQUE_COMPILE_SKIP;

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
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, read_sources);
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
        if (!best || desc->priority > best_priority)
        {
            best = desc;
            best_priority = desc->priority;
        }
    }
    return best;
}

str8 mel_render_technique_name(Mel_Technique_Family_Id family)
{
    const Mel_Technique_Desc* desc = mel_render_technique_get(family);
    return desc ? desc->name : S8("");
}
