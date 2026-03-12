#include "render.technique.h"
#include "render.frame_plan.h"
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

static Mel_Technique_Compile_Result mel__compile_mesh(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_MESH_INSTANCE);
    if (!read_lists)
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
        mel_mesh_pass_execute, mesh_pass, read_lists, nullptr,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
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
        mel__imgui_execute, callback, nullptr, nullptr);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
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
        .compile = mel__compile_sprite,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_TEXT,
        .name = S8("text"),
        .source_schema = MEL_SCHEMA_TEXT,
        .compile = mel__compile_text,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = S8("mesh"),
        .source_schema = MEL_SCHEMA_MESH_INSTANCE,
        .compile = mel__compile_mesh,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_DEBUG,
        .name = S8("debug"),
        .source_schema = MEL_SCHEMA_SPRITE,
        .compile = mel__compile_debug,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_IMGUI,
        .name = S8("imgui"),
        .source_schema = 0,
        .compile = mel__compile_imgui,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_UI,
        .name = S8("ui"),
        .source_schema = MEL_SCHEMA_SPRITE,
        .compile = mel__compile_ui,
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
        if (s_registry.items[i].desc.family == desc->family)
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

const Mel_Technique_Desc* mel_render_technique_get(Mel_Technique_Family_Id family)
{
    assert(s_initialized);
    for (usize i = 0; i < s_registry.count; i++)
        if (s_registry.items[i].desc.family == family)
            return &s_registry.items[i].desc;
    return nullptr;
}

str8 mel_render_technique_name(Mel_Technique_Family_Id family)
{
    const Mel_Technique_Desc* desc = mel_render_technique_get(family);
    return desc ? desc->name : S8("");
}
