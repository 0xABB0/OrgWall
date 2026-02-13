#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "string.str8.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.texture.h"
#include "gpu.cmd.h"
#include "sprite.batch.h"
#include "font.atlas.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.easing.h"

#define EASING_COUNT       31
#define GRID_COLS          7
#define GRID_ROWS          5
#define PANEL_W            140.0f
#define PANEL_H            100.0f
#define PANEL_PAD          12.0f
#define PANEL_INNER_PAD    8.0f
#define GRID_MARGIN_X      30.0f
#define GRID_MARGIN_Y      20.0f
#define CURVE_SAMPLES      50
#define FOCUS_SAMPLES      100
#define FOCUS_W            500.0f
#define FOCUS_H            200.0f
#define FOCUS_BAR_H        20.0f
#define DOT_SIZE           3.0f
#define ANIM_DOT_SIZE      6.0f
#define ANIM_PERIOD        2.0f

typedef struct {
    const char* name;
    f32 (*func)(f32);
} EasingEntry;

typedef struct {
    EasingEntry entries[EASING_COUNT];
    f32 time;
    bool paused;
    i32 hovered;
    i32 selected;
} EasingDemo;

static SDL_Window* s_window;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_pipeline;
static Mel_Gpu_Texture s_white_texture;
static Mel_SpriteBatch s_batch;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Font_Handle s_font_handle;
static EasingDemo s_demo;

static const char* SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float2 position : POSITION;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct VSOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"};\n"
"\n"
"[[vk::push_constant]]\n"
"PushConstants push;\n"
"\n"
"[[vk::binding(0, 0)]] Sampler2D tex;\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));\n"
"    output.texcoord = input.texcoord;\n"
"    output.color = input.color;\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    return tex.Sample(input.texcoord) * input.color;\n"
"}\n";

static void demo_init(EasingDemo* d)
{
    d->time = 0.0f;
    d->paused = false;
    d->hovered = -1;
    d->selected = -1;

    i32 i = 0;
    d->entries[i++] = (EasingEntry){ "linear",         mel_ease_linear };
    d->entries[i++] = (EasingEntry){ "in_quad",        mel_ease_in_quad };
    d->entries[i++] = (EasingEntry){ "out_quad",       mel_ease_out_quad };
    d->entries[i++] = (EasingEntry){ "in_out_quad",    mel_ease_in_out_quad };
    d->entries[i++] = (EasingEntry){ "in_cubic",       mel_ease_in_cubic };
    d->entries[i++] = (EasingEntry){ "out_cubic",      mel_ease_out_cubic };
    d->entries[i++] = (EasingEntry){ "in_out_cubic",   mel_ease_in_out_cubic };
    d->entries[i++] = (EasingEntry){ "in_quart",       mel_ease_in_quart };
    d->entries[i++] = (EasingEntry){ "out_quart",      mel_ease_out_quart };
    d->entries[i++] = (EasingEntry){ "in_out_quart",   mel_ease_in_out_quart };
    d->entries[i++] = (EasingEntry){ "in_quint",       mel_ease_in_quint };
    d->entries[i++] = (EasingEntry){ "out_quint",      mel_ease_out_quint };
    d->entries[i++] = (EasingEntry){ "in_out_quint",   mel_ease_in_out_quint };
    d->entries[i++] = (EasingEntry){ "in_sine",        mel_ease_in_sine };
    d->entries[i++] = (EasingEntry){ "out_sine",       mel_ease_out_sine };
    d->entries[i++] = (EasingEntry){ "in_out_sine",    mel_ease_in_out_sine };
    d->entries[i++] = (EasingEntry){ "in_circ",        mel_ease_in_circ };
    d->entries[i++] = (EasingEntry){ "out_circ",       mel_ease_out_circ };
    d->entries[i++] = (EasingEntry){ "in_out_circ",    mel_ease_in_out_circ };
    d->entries[i++] = (EasingEntry){ "in_expo",        mel_ease_in_expo };
    d->entries[i++] = (EasingEntry){ "out_expo",       mel_ease_out_expo };
    d->entries[i++] = (EasingEntry){ "in_out_expo",    mel_ease_in_out_expo };
    d->entries[i++] = (EasingEntry){ "in_elastic",     mel_ease_in_elastic };
    d->entries[i++] = (EasingEntry){ "out_elastic",    mel_ease_out_elastic };
    d->entries[i++] = (EasingEntry){ "in_out_elastic", mel_ease_in_out_elastic };
    d->entries[i++] = (EasingEntry){ "in_back",        mel_ease_in_back };
    d->entries[i++] = (EasingEntry){ "out_back",       mel_ease_out_back };
    d->entries[i++] = (EasingEntry){ "in_out_back",    mel_ease_in_out_back };
    d->entries[i++] = (EasingEntry){ "in_bounce",      mel_ease_in_bounce };
    d->entries[i++] = (EasingEntry){ "out_bounce",     mel_ease_out_bounce };
    d->entries[i++] = (EasingEntry){ "in_out_bounce",  mel_ease_in_out_bounce };
}

static void panel_pos(i32 index, f32* out_x, f32* out_y)
{
    i32 col = index % GRID_COLS;
    i32 row = index / GRID_COLS;
    *out_x = GRID_MARGIN_X + (f32)col * (PANEL_W + PANEL_PAD);
    *out_y = GRID_MARGIN_Y + (f32)row * (PANEL_H + PANEL_PAD);
}

static i32 hit_test_panel(f32 mx, f32 my)
{
    for (i32 i = 0; i < EASING_COUNT; i++)
    {
        f32 px, py;
        panel_pos(i, &px, &py);
        if (mx >= px && mx < px + PANEL_W && my >= py && my < py + PANEL_H)
            return i;
    }
    return -1;
}

static void draw_curve_dots(Mel_SpriteBatch* batch, f32 px, f32 py, f32 w, f32 h,
                            f32 pad, i32 samples, f32 (*func)(f32), Mel_Vec4 color)
{
    f32 plot_x = px + pad;
    f32 plot_y = py + pad + 14.0f;
    f32 plot_w = w - pad * 2.0f;
    f32 plot_h = h - pad * 2.0f - 14.0f;

    for (i32 i = 0; i <= samples; i++)
    {
        f32 t = (f32)i / (f32)samples;
        f32 v = func(t);
        f32 x = plot_x + t * plot_w;
        f32 y = plot_y + plot_h - v * plot_h;
        mel_sprite_batch_draw(batch, x - DOT_SIZE * 0.5f, y - DOT_SIZE * 0.5f,
                              DOT_SIZE, DOT_SIZE, color);
    }
}

static void draw_anim_dot(Mel_SpriteBatch* batch, f32 px, f32 py, f32 w, f32 h,
                           f32 pad, f32 t, f32 (*func)(f32), Mel_Vec4 color)
{
    f32 plot_x = px + pad;
    f32 plot_y = py + pad + 14.0f;
    f32 plot_w = w - pad * 2.0f;
    f32 plot_h = h - pad * 2.0f - 14.0f;

    f32 v = func(t);
    f32 x = plot_x + t * plot_w;
    f32 y = plot_y + plot_h - v * plot_h;
    mel_sprite_batch_draw(batch, x - ANIM_DOT_SIZE * 0.5f, y - ANIM_DOT_SIZE * 0.5f,
                          ANIM_DOT_SIZE, ANIM_DOT_SIZE, color);
}

static void draw_shapes(EasingDemo* d, Mel_SpriteBatch* batch, f32 win_w, f32 win_h)
{
    MEL_UNUSED(win_w);
    MEL_UNUSED(win_h);

    f32 t = fmodf(d->time, ANIM_PERIOD) / ANIM_PERIOD;

    Mel_Vec4 bg_normal   = mel_vec4(0.14f, 0.14f, 0.18f, 1.0f);
    Mel_Vec4 bg_hovered  = mel_vec4(0.20f, 0.20f, 0.26f, 1.0f);
    Mel_Vec4 bg_selected = mel_vec4(0.22f, 0.18f, 0.28f, 1.0f);
    Mel_Vec4 curve_color = mel_vec4(0.4f, 0.7f, 1.0f, 1.0f);
    Mel_Vec4 dot_color   = mel_vec4(1.0f, 0.4f, 0.3f, 1.0f);

    for (i32 i = 0; i < EASING_COUNT; i++)
    {
        f32 px, py;
        panel_pos(i, &px, &py);

        Mel_Vec4 bg = bg_normal;
        if (i == d->selected) bg = bg_selected;
        else if (i == d->hovered) bg = bg_hovered;

        mel_sprite_batch_draw(batch, px, py, PANEL_W, PANEL_H, bg);

        draw_curve_dots(batch, px, py, PANEL_W, PANEL_H,
                        PANEL_INNER_PAD, CURVE_SAMPLES, d->entries[i].func, curve_color);

        draw_anim_dot(batch, px, py, PANEL_W, PANEL_H,
                      PANEL_INNER_PAD, t, d->entries[i].func, dot_color);
    }

    i32 focus_idx = d->selected >= 0 ? d->selected : d->hovered;
    if (focus_idx < 0) focus_idx = 0;

    f32 grid_bottom = GRID_MARGIN_Y + (f32)GRID_ROWS * (PANEL_H + PANEL_PAD);
    f32 focus_x = GRID_MARGIN_X;
    f32 focus_y = grid_bottom + 10.0f;

    Mel_Vec4 focus_bg = mel_vec4(0.12f, 0.12f, 0.16f, 1.0f);
    mel_sprite_batch_draw(batch, focus_x, focus_y, FOCUS_W, FOCUS_H, focus_bg);

    draw_curve_dots(batch, focus_x, focus_y, FOCUS_W, FOCUS_H,
                    PANEL_INNER_PAD, FOCUS_SAMPLES, d->entries[focus_idx].func, curve_color);

    draw_anim_dot(batch, focus_x, focus_y, FOCUS_W, FOCUS_H,
                  PANEL_INNER_PAD, t, d->entries[focus_idx].func, dot_color);

    f32 bar_x = focus_x + FOCUS_W + 20.0f;
    f32 bar_y = focus_y + FOCUS_H * 0.5f - FOCUS_BAR_H * 0.5f;
    f32 bar_w = 300.0f;

    Mel_Vec4 bar_bg = mel_vec4(0.08f, 0.08f, 0.1f, 1.0f);
    mel_sprite_batch_draw(batch, bar_x, bar_y, bar_w, FOCUS_BAR_H, bar_bg);

    f32 eased = d->entries[focus_idx].func(t);
    f32 fill_w = mel_clampf(eased, 0.0f, 1.5f) * bar_w;
    Mel_Vec4 bar_fill = mel_vec4(0.3f, 0.8f, 0.5f, 1.0f);
    mel_sprite_batch_draw(batch, bar_x, bar_y, fill_w, FOCUS_BAR_H, bar_fill);
}

static void draw_text(EasingDemo* d, Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe)
{
    Mel_Vec4 label_color = mel_vec4(0.7f, 0.7f, 0.7f, 1.0f);
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.5f, 0.5f, 0.5f, 1.0f);

    for (i32 i = 0; i < EASING_COUNT; i++)
    {
        f32 px, py;
        panel_pos(i, &px, &py);
        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(d->entries[i].name),
                                 px + PANEL_INNER_PAD, py + 4.0f, label_color);
    }

    i32 focus_idx = d->selected >= 0 ? d->selected : d->hovered;
    if (focus_idx < 0) focus_idx = 0;

    f32 grid_bottom = GRID_MARGIN_Y + (f32)GRID_ROWS * (PANEL_H + PANEL_PAD);
    f32 focus_x = GRID_MARGIN_X;
    f32 focus_y = grid_bottom + 10.0f;

    char full_name[64];
    snprintf(full_name, sizeof(full_name), "mel_ease_%s", d->entries[focus_idx].name);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(full_name),
                             focus_x + PANEL_INNER_PAD, focus_y + 2.0f, white);

    f32 bar_x = focus_x + FOCUS_W + 20.0f;
    f32 bar_y = focus_y + FOCUS_H * 0.5f - FOCUS_BAR_H * 0.5f;
    mel_font_atlas_draw_text(fe, batch, S8("eased output"),
                             bar_x, bar_y - 18.0f, dim);

    mel_font_atlas_draw_text(fe, batch,
                             d->paused ? S8("[PAUSED] Space=resume  R=reset  ESC=quit")
                                       : S8("Space=pause  R=reset  Click=select  ESC=quit"),
                             GRID_MARGIN_X, focus_y + FOCUS_H + 10.0f, dim);
}

static void on_init(Mel_Engine* e)
{
    Mel_Gpu_Device* dev = &e->dev;

    mel_gpu_shader_init(&s_shader, dev, .source = str8_from_cstr(SHADER_SOURCE));
    mel_gpu_texture_init_white(&s_white_texture, dev);

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Mel_SpriteVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, u) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, r) },
    };

    mel_gpu_pipeline_init(&s_pipeline, dev,
        .shader = &s_shader,
        .color_format = e->swapchain.format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 3,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true,
        .blend_mode = MEL_GPU_BLEND_ALPHA);

    mel_sprite_batch_init(&s_batch, dev, .max_sprites = 4096);

    s_white_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
    mel_gpu_pipeline_write_texture(&s_pipeline, dev, s_white_texture.descriptor,
        s_white_texture.image.view, s_white_texture.sampler);

    mel_font_atlas_pool_init(&s_font_pool, &e->allocator, dev);
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);

    Mel_Font_Atlas_Entry* font_entry = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (font_entry)
    {
        font_entry->atlas_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
        mel_gpu_pipeline_write_texture(&s_pipeline, dev, font_entry->atlas_texture.descriptor,
            font_entry->atlas_texture.image.view, font_entry->atlas_texture.sampler);
    }

    demo_init(&s_demo);
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Easing Visualizer", 1200, 800,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Easing"),
        .enable_validation = true,
        .enable_imgui = false,
        .fixed_dt = 1.0f / 60.0f);

    on_init(&app->engine);
}

static void app_shutdown(Mel_App* app)
{
    Mel_Gpu_Device* dev = &app->engine.dev;
    mel_gpu_device_wait_idle(dev);

    mel_sprite_batch_shutdown(&s_batch, dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_gpu_texture_shutdown(&s_white_texture, dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, dev);
    mel_gpu_shader_shutdown(&s_shader, dev);
}

static void app_update(Mel_App* app, f32 dt)
{
    MEL_UNUSED(app);

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    s_demo.hovered = hit_test_panel(mx, my);

    if (!s_demo.paused)
        s_demo.time += dt;
}

static void app_render(Mel_App* app, Mel_Gpu_Cmd* c)
{
    Mel_Engine* e = &app->engine;
    if (!s_pipeline.pipeline) return;

    mel_engine_begin_swapchain_pass(e, c,
        .clear_r = 0.08f, .clear_g = 0.08f, .clear_b = 0.1f, .clear_a = 1.0f);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                    0, (f32)e->swapchain.extent.height, -1, 1);

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &s_white_texture);

    draw_shapes(&s_demo, &s_batch,
                (f32)e->swapchain.extent.width, (f32)e->swapchain.extent.height);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
        draw_text(&s_demo, &s_batch, fe);

    mel_sprite_batch_end(&s_batch, &e->dev, c->cmd, &proj);
    mel_engine_end_swapchain_pass(e, c);
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_SPACE:
                s_demo.paused = !s_demo.paused;
                break;
            case SDL_SCANCODE_R:
                s_demo.time = 0.0f;
                break;
            default:
                break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        i32 hit = hit_test_panel(event->button.x, event->button.y);
        if (hit >= 0)
        {
            if (s_demo.selected == hit)
                s_demo.selected = -1;
            else
                s_demo.selected = hit;
        }
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_render = app_render,
    .on_event = app_event
)
