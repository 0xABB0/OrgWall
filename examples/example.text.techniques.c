#include <SDL3/SDL.h>

#include "gpu.device.h"

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "string.str8.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "text.draw.h"
#include "render.stage.2d.h"
#include "render.graph.h"
#include "render.list.h"
#include "render.camera.h"
#include "font.atlas.h"
#include "font.sdf.h"
#include "font.msdf.h"
#include "font.desc.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "sim.ctx.h"

#define WIN_W 1360
#define WIN_H 860

typedef struct {
    f32 edge;
    f32 softness;
    f32 outline;
    f32 px_range;
    float color[4];
    float outline_color[4];
} TextTechniqueParams;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_2D s_stage;
static Mel_Render_List s_bg_list;
static Mel_Render_List s_text_list;
static Mel_Camera s_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static Mel_Font_Atlas_Handle s_atlas_font;
static Mel_Font_SDF_Handle s_sdf_font;
static Mel_Font_MSDF_Handle s_msdf_font;

static TextTechniqueParams s_atlas_params = {
    .edge = 0.45f,
    .softness = 0.12f,
    .outline = 0.00f,
    .px_range = 1.0f,
    .color = { 0.96f, 0.91f, 0.79f, 1.0f },
    .outline_color = { 0.06f, 0.06f, 0.09f, 1.0f },
};
static TextTechniqueParams s_sdf_params = {
    .edge = 0.50f,
    .softness = 0.004f,
    .outline = 0.0f,
    .px_range = 8.0f,
    .color = { 0.82f, 0.96f, 0.87f, 1.0f },
    .outline_color = { 0.06f, 0.10f, 0.07f, 1.0f },
};
static TextTechniqueParams s_msdf_params = {
    .edge = 0.50f,
    .softness = 0.004f,
    .outline = 0.0f,
    .px_range = 12.0f,
    .color = { 0.86f, 0.90f, 0.99f, 1.0f },
    .outline_color = { 0.05f, 0.06f, 0.10f, 1.0f },
};

static void texttech_sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    if (sc->extent_width != (u32)w || sc->extent_height != (u32)h)
    {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        s_camera.projection = mel_mat4_ortho(0.0f, (f32)w, (f32)h, 0.0f, -1.0f, 1.0f);
    }
}

static void texttech_imgui_build_pass(Mel_Render_Pass_Ctx* ctx)
{
    MEL_UNUSED(ctx);

    ImGuiIO* io = igGetIO_Nil();
    float display_w = io ? io->DisplaySize.x : (float)WIN_W;
    float panel_w = 340.0f;
    float panel_x = display_w - panel_w - 24.0f;
    if (panel_x < 24.0f)
        panel_x = 24.0f;

    igSetNextWindowPos((ImVec2){ panel_x, 24.0f }, ImGuiCond_Always, (ImVec2){0});
    igSetNextWindowSize((ImVec2){ panel_w, 720.0f }, ImGuiCond_FirstUseEver);
    igSetNextWindowBgAlpha(0.94f);
    if (igBegin("Text Techniques", nullptr, 0))
    {
        igText("Atlas, SDF, and MSDF on the same screen.");
        igSeparator();

        if (igCollapsingHeader_TreeNodeFlags("Atlas", ImGuiTreeNodeFlags_DefaultOpen))
        {
            igSliderFloat("Atlas Edge", &s_atlas_params.edge, 0.0f, 1.0f, "%.3f", 0);
            igSliderFloat("Atlas Softness", &s_atlas_params.softness, 0.001f, 0.25f, "%.3f", 0);
            igSliderFloat("Atlas Outline", &s_atlas_params.outline, 0.0f, 0.4f, "%.3f", 0);
            igColorEdit4("Atlas Color", s_atlas_params.color, 0);
            igColorEdit4("Atlas Outline Color", s_atlas_params.outline_color, 0);
        }

        if (igCollapsingHeader_TreeNodeFlags("SDF", ImGuiTreeNodeFlags_DefaultOpen))
        {
            igSliderFloat("SDF Edge", &s_sdf_params.edge, 0.0f, 1.0f, "%.3f", 0);
            igSliderFloat("SDF Softness", &s_sdf_params.softness, 0.001f, 0.15f, "%.3f", 0);
            igSliderFloat("SDF Outline", &s_sdf_params.outline, 0.0f, 0.4f, "%.3f", 0);
            igSliderFloat("SDF Px Range", &s_sdf_params.px_range, 1.0f, 16.0f, "%.2f", 0);
            igColorEdit4("SDF Color", s_sdf_params.color, 0);
            igColorEdit4("SDF Outline Color", s_sdf_params.outline_color, 0);
        }

        if (igCollapsingHeader_TreeNodeFlags("MSDF", ImGuiTreeNodeFlags_DefaultOpen))
        {
            igSliderFloat("MSDF Edge", &s_msdf_params.edge, 0.0f, 1.0f, "%.3f", 0);
            igSliderFloat("MSDF Softness", &s_msdf_params.softness, 0.001f, 0.15f, "%.3f", 0);
            igSliderFloat("MSDF Outline", &s_msdf_params.outline, 0.0f, 0.4f, "%.3f", 0);
            igSliderFloat("MSDF Px Range", &s_msdf_params.px_range, 1.0f, 16.0f, "%.2f", 0);
            igColorEdit4("MSDF Color", s_msdf_params.color, 0);
            igColorEdit4("MSDF Outline Color", s_msdf_params.outline_color, 0);
        }
    }
    igEnd();
}

static void texttech_imgui_render_pass(Mel_Render_Pass_Ctx* ctx)
{
    MEL_UNUSED(ctx);
    igRender();
    ImDrawData* draw_data = igGetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0)
        ImGui_ImplVulkan_RenderDrawData(draw_data, ctx->cmd._cmd, VK_NULL_HANDLE);
}

static void texttech_add_panel(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_draw_sprite(list,
        .pos = mel_vec2(x, y),
        .size = mel_vec2(w, h),
        .color = color,
        .tex = (Mel_Texture_Handle){0},
        .uv = MEL_UV_FULL);
}

static Mel_Text_Style texttech_style_from(const TextTechniqueParams* p)
{
    Mel_Text_Style style = mel_text_style(mel_vec4(p->color[0], p->color[1], p->color[2], p->color[3]));
    style.outline_color = mel_vec4(p->outline_color[0], p->outline_color[1], p->outline_color[2], p->outline_color[3]);
    style.edge = p->edge;
    style.softness = p->softness;
    style.outline = p->outline;
    style.px_range = p->px_range;
    return style;
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    texttech_sync_viewport();

    i32 viewport_w = 0;
    i32 viewport_h = 0;
    mel_window_size_pixels(s_window_handle, &viewport_w, &viewport_h);
    f32 panel_x = 44.0f;
    f32 panel_w = (f32)viewport_w - 460.0f;
    if (panel_w < 420.0f) panel_w = 420.0f;
    if (panel_w > 760.0f) panel_w = 760.0f;
    f32 panel_gap = 24.0f;
    f32 panel1_y = 52.0f;
    f32 panel1_h = 280.0f;
    f32 panel2_y = panel1_y + panel1_h + panel_gap;
    f32 panel2_h = 260.0f;
    f32 panel3_y = panel2_y + panel2_h + panel_gap;
    f32 panel3_h = 180.0f;
    f32 text_x = panel_x + 28.0f;

    mel_render_list_clear(&s_bg_list);
    mel_render_list_clear(&s_text_list);

    texttech_add_panel(&s_bg_list, panel_x, panel1_y, panel_w, panel1_h, mel_vec4(0.20f, 0.15f, 0.11f, 1.0f));
    texttech_add_panel(&s_bg_list, panel_x, panel2_y, panel_w, panel2_h, mel_vec4(0.10f, 0.18f, 0.13f, 1.0f));
    texttech_add_panel(&s_bg_list, panel_x, panel3_y, panel_w, panel3_h, mel_vec4(0.11f, 0.13f, 0.21f, 1.0f));

    Mel_Text_Style atlas = texttech_style_from(&s_atlas_params);
    Mel_Text_Style sdf = texttech_style_from(&s_sdf_params);
    Mel_Text_Style msdf = texttech_style_from(&s_msdf_params);

    mel_text_draw_font_atlas(s_atlas_font, &s_text_list, S8("Bitmap Atlas"),
        .x = text_x, .y = panel1_y + 32.0f, .style = atlas);
    mel_text_draw_font_atlas(s_atlas_font, &s_text_list,
        S8("Fast, crisp at the native size,\nbut scaling exposes the pixels."),
        .x = text_x, .y = panel1_y + 76.0f, .style = atlas);

    mel_text_draw_font_sdf(s_sdf_font, &s_text_list, S8("Signed Distance Field"),
        .x = text_x, .y = panel2_y + 30.0f, .scale = 0.70f, .style = sdf);
    mel_text_draw_font_sdf(s_sdf_font, &s_text_list,
        S8("Smooth scaling and cheap effects.\nCorners get a little rounder,\nbut fine details soften first."),
        .x = text_x, .y = panel2_y + 76.0f, .scale = 0.70f, .style = sdf);

    mel_text_draw_font_msdf(s_msdf_font, &s_text_list, S8("Multi-Channel SDF"),
        .x = text_x, .y = panel3_y + 24.0f, .scale = 0.17f, .style = msdf);
    mel_text_draw_font_msdf(s_msdf_font, &s_text_list,
        S8("Sharper corners and cleaner outlines.\nBest when you need scale headroom."),
        .x = text_x, .y = panel3_y + 70.0f, .scale = 0.17f, .style = msdf);
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_render_list_init(&s_bg_list,
        .name = S8("texttech_bg"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_text_list,
        .name = S8("texttech_text"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0.0f, (f32)sc->extent_width, (f32)sc->extent_height, 0.0f, -1.0f, 1.0f),
    };

    Mel_Font_Desc_Handle monaco = mel_font_desc_load_ttf(S8("/System/Library/Fonts/Monaco.ttf"));
    s_atlas_font = mel_font_atlas_load(
        .desc = monaco, .size = 28.0f);
    s_sdf_font = mel_font_sdf_load(
        .desc = monaco, .size = 40.0f);
    s_msdf_font = mel_font_msdf_load(
        .desc = monaco,
        .size = 128.0f,
        .atlas_width = 2048,
        .atlas_height = 2048,
        .px_range = 12.0f,
        .padding = 18);

    mel_render_stage_2d_init(&s_stage,
        .name = S8("texttech"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_camera,
        .hud_camera = &s_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.05f, 0.06f, 0.08f, 1.0f),
        .install_as_current_graph = false,
        .dev = dev,
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    mel_render_stage_2d_attach_sprite_list(&s_stage, &s_bg_list);
    mel_render_stage_2d_attach_text_list_to_layer(&s_stage, MEL_RENDER_STAGE_2D_LAYER_HUD, &s_text_list);
    mel_render_stage_2d_rebuild(&s_stage);

    Mel_Render_Graph* graph = mel_render_stage_2d_graph(&s_stage);
    u32 last_stage_pass = (u32)(graph->passes.count - 1);
    u32 imgui_build_pass = mel_render_graph_add_pass(graph, S8("texttech.imgui.build"),
        .fn = texttech_imgui_build_pass);
    u32 imgui_render_pass = mel_render_graph_add_pass(graph, S8("texttech.imgui.render"),
        .fn = texttech_imgui_render_pass,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = mel_render_stage_2d_target(&s_stage), .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }));
    mel_render_graph_pass_depends_on(graph, imgui_build_pass, last_stage_pass);
    mel_render_graph_pass_depends_on(graph, imgui_render_pass, imgui_build_pass);
    mel_render_graph_compile(graph);
    mel_set_render_graph(graph);

    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Text Techniques"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_render_stage_2d_shutdown(&s_stage);
    mel_render_list_shutdown(&s_text_list);
    mel_render_list_shutdown(&s_bg_list);

    mel_vfs_unmount(S8("/"));
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
