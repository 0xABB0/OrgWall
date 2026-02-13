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
#include "anim.state.h"
#include "anim.timeline.h"

#define WIDTH  640
#define HEIGHT 480

#define SPRITE_SRC_SIZE 32
#define SPRITE_SCALE    3
#define SPRITE_SIZE     (SPRITE_SRC_SIZE * SPRITE_SCALE)

#define STATE_IDLE 0
#define STATE_WALK 1
#define STATE_RUN  2
#define STATE_HIT  3
#define STATE_COUNT 4

#define COND_WALK 1
#define COND_RUN  2
#define COND_IDLE 3
#define COND_HIT  4

#define IDLE_FRAME_COUNT 1
#define WALK_FRAME_COUNT 4
#define RUN_FRAME_COUNT  4
#define HIT_FRAME_COUNT  3

typedef struct {
    Mel_Anim_State_Player player;
    Mel_Anim_State_Machine machine;
    Mel_Anim_State_Def state_defs[STATE_COUNT];
    Mel_Anim_Timeline timelines[STATE_COUNT];
    Mel_Anim_Keyframe idle_kf[1];
    Mel_Anim_Keyframe walk_kf[4];
    Mel_Anim_Keyframe run_kf[4];
    Mel_Anim_Keyframe hit_kf[3];
    Mel_Anim_Transition idle_trans[3];
    Mel_Anim_Transition walk_trans[3];
    Mel_Anim_Transition run_trans[3];
    Mel_Anim_Transition hit_trans[1];
    u64 condition_storage[64];
    u32 prev_state;
} AnimDemo;

static SDL_Window* s_window;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_pipeline;
static Mel_Gpu_Texture s_white_texture;
static Mel_SpriteBatch s_batch;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Font_Handle s_font_handle;

static Mel_Gpu_Texture s_idle_tex;
static Mel_Gpu_Texture s_walk_tex;
static Mel_Gpu_Texture s_run_tex;
static Mel_Gpu_Texture s_hit_tex;
static AnimDemo s_demo;

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

static const u32 s_frame_counts[STATE_COUNT] = {
    IDLE_FRAME_COUNT,
    WALK_FRAME_COUNT,
    RUN_FRAME_COUNT,
    HIT_FRAME_COUNT,
};

static const char* s_state_names[STATE_COUNT] = {
    "IDLE",
    "WALK",
    "RUN",
    "HIT",
};

static void init_texture(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, str8 path)
{
    mel_gpu_texture_init(tex, dev, .path = path, .nearest_filter = true);
    tex->descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
    mel_gpu_pipeline_write_texture(&s_pipeline, dev, tex->descriptor,
        tex->image.view, tex->sampler);
}

static Mel_Gpu_Texture* texture_for_state(u32 state)
{
    switch (state)
    {
        case STATE_IDLE: return &s_idle_tex;
        case STATE_WALK: return &s_walk_tex;
        case STATE_RUN:  return &s_run_tex;
        case STATE_HIT:  return &s_hit_tex;
        default:         return &s_idle_tex;
    }
}

static void clear_all_conditions(Mel_Anim_State_Player* player)
{
    mel_anim_state_player_set_condition(player, COND_WALK, false);
    mel_anim_state_player_set_condition(player, COND_RUN, false);
    mel_anim_state_player_set_condition(player, COND_IDLE, false);
    mel_anim_state_player_set_condition(player, COND_HIT, false);
}

static void anim_demo_init(AnimDemo* d)
{
    memset(d, 0, sizeof(*d));

    d->idle_kf[0] = (Mel_Anim_Keyframe){ .frame_index = 0, .duration = 1.0f };

    d->walk_kf[0] = (Mel_Anim_Keyframe){ .frame_index = 0, .duration = 0.15f };
    d->walk_kf[1] = (Mel_Anim_Keyframe){ .frame_index = 1, .duration = 0.15f };
    d->walk_kf[2] = (Mel_Anim_Keyframe){ .frame_index = 2, .duration = 0.15f };
    d->walk_kf[3] = (Mel_Anim_Keyframe){ .frame_index = 3, .duration = 0.15f };

    d->run_kf[0] = (Mel_Anim_Keyframe){ .frame_index = 0, .duration = 0.10f };
    d->run_kf[1] = (Mel_Anim_Keyframe){ .frame_index = 1, .duration = 0.10f };
    d->run_kf[2] = (Mel_Anim_Keyframe){ .frame_index = 2, .duration = 0.10f };
    d->run_kf[3] = (Mel_Anim_Keyframe){ .frame_index = 3, .duration = 0.10f };

    d->hit_kf[0] = (Mel_Anim_Keyframe){ .frame_index = 0, .duration = 0.12f };
    d->hit_kf[1] = (Mel_Anim_Keyframe){ .frame_index = 1, .duration = 0.12f };
    d->hit_kf[2] = (Mel_Anim_Keyframe){ .frame_index = 2, .duration = 0.12f };

    d->timelines[STATE_IDLE] = (Mel_Anim_Timeline){
        .keyframes = d->idle_kf,
        .events = nullptr,
        .keyframe_count = IDLE_FRAME_COUNT,

        .loop = true,
    };

    d->timelines[STATE_WALK] = (Mel_Anim_Timeline){
        .keyframes = d->walk_kf,
        .events = nullptr,
        .keyframe_count = WALK_FRAME_COUNT,

        .loop = true,
    };

    d->timelines[STATE_RUN] = (Mel_Anim_Timeline){
        .keyframes = d->run_kf,
        .events = nullptr,
        .keyframe_count = RUN_FRAME_COUNT,

        .loop = true,
    };

    d->timelines[STATE_HIT] = (Mel_Anim_Timeline){
        .keyframes = d->hit_kf,
        .events = nullptr,
        .keyframe_count = HIT_FRAME_COUNT,

        .loop = false,
    };

    d->idle_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_WALK,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_WALK,
        .blend_duration = 0.0f,
    };
    d->idle_trans[1] = (Mel_Anim_Transition){
        .target_state = STATE_RUN,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_RUN,
        .blend_duration = 0.0f,
    };
    d->idle_trans[2] = (Mel_Anim_Transition){
        .target_state = STATE_HIT,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_HIT,
        .blend_duration = 0.0f,
    };

    d->walk_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_RUN,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_RUN,
        .blend_duration = 0.0f,
    };
    d->walk_trans[1] = (Mel_Anim_Transition){
        .target_state = STATE_IDLE,
        .mode = MEL_ANIM_TRANSITION_AT_END,
        .condition_hash = COND_IDLE,
        .blend_duration = 0.0f,
    };
    d->walk_trans[2] = (Mel_Anim_Transition){
        .target_state = STATE_HIT,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_HIT,
        .blend_duration = 0.0f,
    };

    d->run_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_IDLE,
        .mode = MEL_ANIM_TRANSITION_AT_END,
        .condition_hash = COND_IDLE,
        .blend_duration = 0.0f,
    };
    d->run_trans[1] = (Mel_Anim_Transition){
        .target_state = STATE_WALK,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_WALK,
        .blend_duration = 0.0f,
    };
    d->run_trans[2] = (Mel_Anim_Transition){
        .target_state = STATE_HIT,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_HIT,
        .blend_duration = 0.0f,
    };

    d->hit_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_IDLE,
        .mode = MEL_ANIM_TRANSITION_AT_END,
        .condition_hash = COND_HIT,
        .blend_duration = 0.0f,
    };

    d->state_defs[STATE_IDLE] = (Mel_Anim_State_Def){
        .name_hash = STATE_IDLE,
        .timeline = &d->timelines[STATE_IDLE],
        .transitions = d->idle_trans,
        .transition_count = 3,
    };

    d->state_defs[STATE_WALK] = (Mel_Anim_State_Def){
        .name_hash = STATE_WALK,
        .timeline = &d->timelines[STATE_WALK],
        .transitions = d->walk_trans,
        .transition_count = 3,
    };

    d->state_defs[STATE_RUN] = (Mel_Anim_State_Def){
        .name_hash = STATE_RUN,
        .timeline = &d->timelines[STATE_RUN],
        .transitions = d->run_trans,
        .transition_count = 3,
    };

    d->state_defs[STATE_HIT] = (Mel_Anim_State_Def){
        .name_hash = STATE_HIT,
        .timeline = &d->timelines[STATE_HIT],
        .transitions = d->hit_trans,
        .transition_count = 1,
    };

    d->machine = (Mel_Anim_State_Machine){
        .states = d->state_defs,
        .state_count = STATE_COUNT,
        .initial_state = STATE_IDLE,
        .alloc = nullptr,
    };

    mel_anim_state_player_init(&d->player, &d->machine);
    d->player.active_conditions = d->condition_storage;
    d->prev_state = STATE_IDLE;
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

    mel_sprite_batch_init(&s_batch, dev, .max_sprites = 2048);

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

    init_texture(&s_idle_tex, dev, S8("assets/hero/idle_DOWN.png"));
    init_texture(&s_walk_tex, dev, S8("assets/hero/walk_DOWN.png"));
    init_texture(&s_run_tex, dev, S8("assets/hero/run_DOWN.png"));
    init_texture(&s_hit_tex, dev, S8("assets/hero/hit_DOWN.png"));

    anim_demo_init(&s_demo);

    SDL_Log("Anim demo ready! W=Walk, R=Run, I=Idle, H=Hit, ESC=Quit");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Animation Showcase", WIDTH, HEIGHT,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Animation Showcase"),
        .enable_validation = true,
        .enable_imgui = false,
        .fixed_dt = 1.0f / 60.0f);

    on_init(&app->engine);
}

static void app_shutdown(Mel_App* app)
{
    Mel_Gpu_Device* dev = &app->engine.dev;
    mel_gpu_device_wait_idle(dev);

    mel_gpu_texture_shutdown(&s_idle_tex, dev);
    mel_gpu_texture_shutdown(&s_walk_tex, dev);
    mel_gpu_texture_shutdown(&s_run_tex, dev);
    mel_gpu_texture_shutdown(&s_hit_tex, dev);

    mel_sprite_batch_shutdown(&s_batch, dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_gpu_texture_shutdown(&s_white_texture, dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, dev);
    mel_gpu_shader_shutdown(&s_shader, dev);
}

static void app_update(Mel_App* app, f32 dt)
{
    MEL_UNUSED(app);

    AnimDemo* d = &s_demo;
    d->prev_state = d->player.current_state;
    mel_anim_state_player_update(&d->player, dt);

    if (d->prev_state == STATE_HIT && d->player.current_state == STATE_IDLE)
    {
        clear_all_conditions(&d->player);
    }
}

static void draw_hero(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev, AnimDemo* d, f32 cx, f32 cy)
{
    u32 state = d->player.current_state;
    u32 frame = mel_anim_state_player_frame_index(&d->player);
    u32 frame_count = s_frame_counts[state];
    Mel_Gpu_Texture* tex = texture_for_state(state);

    f32 u0 = (f32)frame / (f32)frame_count;
    f32 u1 = (f32)(frame + 1) / (f32)frame_count;

    mel_sprite_batch_set_texture(batch, tex);

    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_sprite_batch_draw_uv(batch,
        cx - SPRITE_SIZE / 2.0f,
        cy - SPRITE_SIZE / 2.0f,
        (f32)SPRITE_SIZE, (f32)SPRITE_SIZE,
        u0, 0.0f, u1, 1.0f,
        white);
}

static void draw_info(Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe, AnimDemo* d, f32 cx, f32 y)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.5f, 0.5f, 0.5f, 1.0f);
    Mel_Vec4 green = mel_vec4(0.3f, 0.9f, 0.3f, 1.0f);

    char buf[128];
    u32 state = d->player.current_state;
    u32 frame = mel_anim_state_player_frame_index(&d->player);

    snprintf(buf, sizeof(buf), "State: %s", s_state_names[state]);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), cx - 60.0f, y, green);

    snprintf(buf, sizeof(buf), "Frame: %u / %u", frame, s_frame_counts[state]);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), cx - 60.0f, y + 24.0f, white);

    f32 controls_y = y + 64.0f;
    mel_font_atlas_draw_text(fe, batch, S8("[W] Walk  [R] Run"), cx - 80.0f, controls_y, dim);
    mel_font_atlas_draw_text(fe, batch, S8("[I] Idle  [H] Hit"), cx - 80.0f, controls_y + 22.0f, dim);
    mel_font_atlas_draw_text(fe, batch, S8("[ESC] Quit"), cx - 80.0f, controls_y + 44.0f, dim);

    f32 diagram_y = controls_y + 84.0f;
    Mel_Vec4 cyan = mel_vec4(0.3f, 0.8f, 0.9f, 1.0f);
    mel_font_atlas_draw_text(fe, batch, S8("IDLE --W--> WALK --R--> RUN"), cx - 140.0f, diagram_y, cyan);
    mel_font_atlas_draw_text(fe, batch, S8("  ^           |           |"), cx - 140.0f, diagram_y + 18.0f, cyan);
    mel_font_atlas_draw_text(fe, batch, S8("  +----I------+-----I-----+"), cx - 140.0f, diagram_y + 36.0f, cyan);
    mel_font_atlas_draw_text(fe, batch, S8("  ^                        "), cx - 140.0f, diagram_y + 54.0f, cyan);
    mel_font_atlas_draw_text(fe, batch, S8("  +------HIT<---H----------"), cx - 140.0f, diagram_y + 72.0f, cyan);
}

static void app_render(Mel_App* app, Mel_Gpu_Cmd* c)
{
    Mel_Engine* e = &app->engine;

    if (!s_pipeline.pipeline) return;

    mel_engine_begin_swapchain_pass(e, c,
        .clear_r = 0.08f, .clear_g = 0.08f, .clear_b = 0.1f, .clear_a = 1.0f);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                    0, (f32)e->swapchain.extent.height, -1, 1);

    f32 cx = (f32)e->swapchain.extent.width / 2.0f;
    f32 hero_y = (f32)e->swapchain.extent.height * 0.33f;

    mel_sprite_batch_begin(&s_batch, &s_pipeline);

    draw_hero(&s_batch, &e->dev, &s_demo, cx, hero_y);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
    {
        draw_info(&s_batch, fe, &s_demo, cx, hero_y + SPRITE_SIZE / 2.0f + 20.0f);
    }

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
        AnimDemo* d = &s_demo;

        switch (event->key.scancode)
        {
            case SDL_SCANCODE_W:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_WALK, true);
                break;
            case SDL_SCANCODE_R:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_RUN, true);
                break;
            case SDL_SCANCODE_I:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_IDLE, true);
                break;
            case SDL_SCANCODE_H:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_HIT, true);
                break;
            default: break;
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
