#include <SDL3/SDL.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "string.str8.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.cmd.h"
#include "render.list.h"
#include "texture.pool.h"
#include "sprite.pass.h"
#include "font.atlas.h"
#include "vfs.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "collection.skiplist.h"
#include "render.graph.h"
#include "render.target.h"
#include "render.camera.h"
#include "allocator.heap.h"
#include "sim.ctx.h"

#include <stdint.h>

#define BOX_W 44.0f
#define BOX_H 24.0f
#define LEVEL_GAP 4.0f
#define NODE_GAP 50.0f
#define START_X 60.0f
#define PANEL_W 260.0f
#define PANEL_PAD 16.0f
#define LINE_H 2.0f
#define ARROW_SIZE 4.0f
#define MAX_VISIBLE_NODES 128

#define OP_NONE   0
#define OP_INSERT 1
#define OP_REMOVE 2
#define OP_FIND   3
#define OP_CLEAR  4
#define OP_SEED   5

typedef struct {
    Mel_SkipList list;
    char input_buf[16];
    i32 input_len;
    i32 highlighted_key;
    bool has_highlight;
    f32 highlight_timer;
    bool show_search_path;
    f32 search_path_timer;
    i32 search_path_keys[MEL_SKIPLIST_MAX_LEVEL];
    i32 search_path_len;
    u32 last_op;
    i32 last_op_key;
    u64 seed_counter;
    u64 rng;
} SkipListDemo;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Font_Handle s_font_handle;
static SkipListDemo s_demo;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Render_List s_sprite_list;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static i32 int_compare(const void* a, const void* b)
{
    intptr_t ia = (intptr_t)a;
    intptr_t ib = (intptr_t)b;
    return (ia > ib) - (ia < ib);
}

static u32 demo_rand(u64* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (u32)(*state & 0xFFFFFFFF);
}

static void record_search_path(SkipListDemo* demo, i32 key)
{
    demo->search_path_len = 0;
    Mel_SkipNode* x = demo->list.header;
    for (i32 i = (i32)demo->list.level - 1; i >= 0; i--)
    {
        while (x->forward[i] && demo->list.cmp(x->forward[i]->key, (void*)(intptr_t)key) < 0)
        {
            x = x->forward[i];
            if (demo->search_path_len < MEL_SKIPLIST_MAX_LEVEL)
            {
                demo->search_path_keys[demo->search_path_len] = (i32)(intptr_t)x->key;
                demo->search_path_len++;
            }
        }
    }
}

static i32 parse_input(SkipListDemo* demo)
{
    if (demo->input_len == 0) return -1;
    i32 val = 0;
    for (i32 i = 0; i < demo->input_len; i++)
        val = val * 10 + (demo->input_buf[i] - '0');
    return val;
}

static void clear_input(SkipListDemo* demo)
{
    demo->input_len = 0;
    demo->input_buf[0] = '\0';
}

static void demo_insert(SkipListDemo* demo, i32 key)
{
    mel_skiplist_insert(&demo->list, (void*)(intptr_t)key, nullptr);
    demo->last_op = OP_INSERT;
    demo->last_op_key = key;
    demo->has_highlight = true;
    demo->highlighted_key = key;
    demo->highlight_timer = 2.0f;
}

static void demo_remove(SkipListDemo* demo, i32 key)
{
    mel_skiplist_remove(&demo->list, (void*)(intptr_t)key);
    demo->last_op = OP_REMOVE;
    demo->last_op_key = key;
    demo->has_highlight = false;
    demo->show_search_path = false;
}

static void demo_find(SkipListDemo* demo, i32 key)
{
    record_search_path(demo, key);
    Mel_SkipNode* found = mel_skiplist_find(&demo->list, (void*)(intptr_t)key);
    demo->last_op = OP_FIND;
    demo->last_op_key = key;
    demo->show_search_path = true;
    demo->search_path_timer = 3.0f;
    if (found)
    {
        demo->has_highlight = true;
        demo->highlighted_key = key;
        demo->highlight_timer = 3.0f;
    }
    else
    {
        demo->has_highlight = false;
    }
}

static void skiplist_demo_init(SkipListDemo* demo)
{
    memset(demo, 0, sizeof(*demo));
    mel_skiplist_init(&demo->list, int_compare, mel_alloc_heap());
    demo->seed_counter = 1;
    mel_skiplist_seed(&demo->list, demo->seed_counter);
    demo->rng = SDL_GetTicks();
    if (demo->rng == 0) demo->rng = 42;
}

static bool is_on_search_path(SkipListDemo* demo, i32 key)
{
    if (!demo->show_search_path) return false;
    for (i32 i = 0; i < demo->search_path_len; i++)
    {
        if (demo->search_path_keys[i] == key) return true;
    }
    return false;
}

typedef struct {
    Mel_SkipNode* node;
    f32 x;
    i32 key;
} NodePos;

static void push_rect(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color, Mel_Texture_Handle tex)
{
    Mel_Sprite_Entry* entry = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, mel_texture_bucket(tex)));
    *entry = (Mel_Sprite_Entry){
        .pos = mel_vec2(x, y),
        .size = mel_vec2(w, h),
        .uv = MEL_UV_FULL,
        .color = color,
        .tex = tex,
    };
}

static void skiplist_draw(SkipListDemo* demo, Mel_Render_List* list,
                          Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font,
                          f32 win_w, f32 win_h)
{
    Mel_Texture_Handle white = MEL_TEXTURE_HANDLE_NULL;
    f32 panel_x = win_w - PANEL_W;

    push_rect(list, panel_x, 0.0f, PANEL_W, win_h,
        mel_vec4(0.1f, 0.1f, 0.12f, 1.0f), white);

    push_rect(list, panel_x, 0.0f, 2.0f, win_h,
        mel_vec4(0.25f, 0.25f, 0.3f, 1.0f), white);

    NodePos positions[MAX_VISIBLE_NODES];
    i32 node_count = 0;

    Mel_SkipNode* node = demo->list.header->forward[0];
    while (node && node_count < MAX_VISIBLE_NODES)
    {
        positions[node_count].node = node;
        positions[node_count].x = START_X + (f32)(node_count + 1) * (BOX_W + NODE_GAP);
        positions[node_count].key = (i32)(intptr_t)node->key;
        node_count++;
        node = node->forward[0];
    }

    u32 max_level = demo->list.level;
    if (max_level == 0) max_level = 1;

    f32 bottom_y = win_h - 60.0f;
    f32 header_x = 10.0f;

    Mel_Vec4 header_color = mel_vec4(0.4f, 0.4f, 0.4f, 1.0f);
    Mel_Vec4 line_color = mel_vec4(0.5f, 0.5f, 0.5f, 0.6f);
    Mel_Vec4 search_line_color = mel_vec4(0.9f, 0.8f, 0.2f, 0.8f);

    for (u32 lvl = 0; lvl < max_level; lvl++)
    {
        f32 y = bottom_y - (f32)lvl * (BOX_H + LEVEL_GAP);
        push_rect(list, header_x, y, BOX_W, BOX_H, header_color, white);
    }

    for (i32 i = 0; i < node_count; i++)
    {
        Mel_SkipNode* n = positions[i].node;
        f32 nx = positions[i].x;
        bool highlighted = demo->has_highlight && positions[i].key == demo->highlighted_key;
        bool on_path = is_on_search_path(demo, positions[i].key);

        for (u32 lvl = 0; lvl < n->level; lvl++)
        {
            f32 y = bottom_y - (f32)lvl * (BOX_H + LEVEL_GAP);

            Mel_Vec4 color;
            if (highlighted)
                color = mel_vec4(0.9f, 0.8f, 0.2f, 1.0f);
            else if (on_path)
                color = mel_vec4(0.7f, 0.6f, 0.15f, 0.8f);
            else if (lvl == 0)
                color = mel_vec4(0.3f, 0.5f, 0.8f, 1.0f);
            else
            {
                f32 t = (f32)lvl / (f32)(max_level > 1 ? max_level - 1 : 1);
                color = mel_vec4(0.3f + t * 0.3f, 0.5f + t * 0.2f, 0.8f - t * 0.2f, 1.0f);
            }

            push_rect(list, nx, y, BOX_W, BOX_H, color, white);
        }
    }

    for (u32 lvl = 0; lvl < max_level; lvl++)
    {
        f32 y = bottom_y - (f32)lvl * (BOX_H + LEVEL_GAP) + BOX_H / 2.0f - LINE_H / 2.0f;

        Mel_SkipNode* curr = demo->list.header;
        f32 from_x = header_x + BOX_W;

        while (curr)
        {
            Mel_SkipNode* next = (lvl < curr->level) ? curr->forward[lvl] : nullptr;
            if (!next) break;

            f32 to_x = 0.0f;
            for (i32 i = 0; i < node_count; i++)
            {
                if (positions[i].node == next)
                {
                    to_x = positions[i].x;
                    break;
                }
            }

            if (to_x <= from_x) break;

            bool path_line = false;
            if (demo->show_search_path && curr != demo->list.header)
            {
                i32 curr_key = (i32)(intptr_t)curr->key;
                if (is_on_search_path(demo, curr_key))
                    path_line = true;
            }
            else if (demo->show_search_path && curr == demo->list.header)
            {
                path_line = true;
            }

            Mel_Vec4 lc = path_line ? search_line_color : line_color;
            push_rect(list, from_x, y, to_x - from_x, LINE_H, lc, white);

            push_rect(list, to_x - ARROW_SIZE, y - ARROW_SIZE / 2.0f,
                ARROW_SIZE, LINE_H + ARROW_SIZE, lc, white);

            from_x = to_x + BOX_W;
            curr = next;
        }
    }

    if (pool)
    {
        Mel_Vec4 w = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
        Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);
        Mel_Vec4 yellow = mel_vec4(0.9f, 0.8f, 0.2f, 1.0f);

        for (u32 lvl = 0; lvl < max_level; lvl++)
        {
            f32 y = bottom_y - (f32)lvl * (BOX_H + LEVEL_GAP) + 4.0f;
            mel_font_atlas_draw_text(pool, font, list, S8("H"), header_x + 16.0f, y, w);
        }

        char buf[64];
        for (i32 i = 0; i < node_count; i++)
        {
            f32 nx = positions[i].x;
            f32 y = bottom_y + 4.0f;
            snprintf(buf, sizeof(buf), "%d", positions[i].key);
            str8 label = str8_from_cstr(buf);
            Mel_Vec2 sz = mel_font_atlas_measure_text(pool, font, label);
            f32 text_x = nx + (BOX_W - sz.x) / 2.0f;
            mel_font_atlas_draw_text(pool, font, list, label, text_x, y, w);
        }

        f32 tx = panel_x + PANEL_PAD;
        f32 ty = PANEL_PAD;

        mel_font_atlas_draw_text(pool, font, list, S8("SKIP LIST"), tx, ty, w);
        ty += 30.0f;

        push_rect(list, panel_x + PANEL_PAD, ty, PANEL_W - PANEL_PAD * 2, 1.0f,
            mel_vec4(0.3f, 0.3f, 0.35f, 1.0f), white);
        ty += 12.0f;

        snprintf(buf, sizeof(buf), "Count: %zu", mel_skiplist_count(&demo->list));
        mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), tx, ty, dim);
        ty += 24.0f;

        snprintf(buf, sizeof(buf), "Levels: %u", demo->list.level);
        mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), tx, ty, dim);
        ty += 24.0f;

        snprintf(buf, sizeof(buf), "Seed: %llu", (unsigned long long)demo->seed_counter);
        mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), tx, ty, dim);
        ty += 36.0f;

        mel_font_atlas_draw_text(pool, font, list, S8("Input:"), tx, ty, dim);
        ty += 24.0f;

        push_rect(list, tx, ty, PANEL_W - PANEL_PAD * 3, 26.0f,
            mel_vec4(0.15f, 0.15f, 0.18f, 1.0f), white);

        if (demo->input_len > 0)
        {
            char display[17];
            memcpy(display, demo->input_buf, (usize)demo->input_len);
            display[demo->input_len] = '\0';
            mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(display), tx + 6.0f, ty + 4.0f, w);
        }
        else
        {
            mel_font_atlas_draw_text(pool, font, list, S8("_"), tx + 6.0f, ty + 4.0f, dim);
        }
        ty += 38.0f;

        {
            const char* op_str = "None";
            switch (demo->last_op)
            {
                case OP_INSERT: op_str = "Insert"; break;
                case OP_REMOVE: op_str = "Remove"; break;
                case OP_FIND:   op_str = "Find";   break;
                case OP_CLEAR:  op_str = "Clear";  break;
                case OP_SEED:   op_str = "Reseed"; break;
                default: break;
            }

            char op_buf[64];
            if (demo->last_op == OP_CLEAR || demo->last_op == OP_SEED || demo->last_op == OP_NONE)
                snprintf(op_buf, sizeof(op_buf), "Last: %s", op_str);
            else
                snprintf(op_buf, sizeof(op_buf), "Last: %s %d", op_str, demo->last_op_key);

            mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(op_buf), tx, ty, yellow);
        }
        ty += 40.0f;

        push_rect(list, panel_x + PANEL_PAD, ty, PANEL_W - PANEL_PAD * 2, 1.0f,
            mel_vec4(0.3f, 0.3f, 0.35f, 1.0f), white);
        ty += 16.0f;

        mel_font_atlas_draw_text(pool, font, list, S8("Controls:"), tx, ty, w);
        ty += 28.0f;

        mel_font_atlas_draw_text(pool, font, list, S8("0-9     type number"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("Enter   insert"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("Delete  remove"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("F       find"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("Space   random insert"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("I       10x random"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("S       reseed+clear"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("C       clear all"), tx, ty, dim);
        ty += 22.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("Esc     quit"), tx, ty, dim);
    }
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    s_font_handle = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    skiplist_demo_init(&s_demo);

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .initial_capacity = 2048,
        .alloc = mel_alloc_heap());

    mel_render_target_init_swapchain(&s_swapchain_target, sc, dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent.width,
                                      0, (f32)sc->extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&s_graph, S8("render"),
        .fn = mel_sprite_pass_execute,
        .user = mel_sprite_pass(),
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    SDL_Log("SkipList demo ready! Type numbers, Enter to insert, Delete to remove, F to find, ESC to quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody SkipList"),
        .enable_validation = true,
    };
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Skip List"), .width = 1100, .height = 700);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_vfs_mount_native(mel_vfs(), S8("/"), S8("/"), 0, false);

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);
    mel_render_list_shutdown(&s_sprite_list);

    mel_skiplist_free(&s_demo.list);

    mel_vfs_unmount(mel_vfs(), S8("/"));
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    if (s_demo.has_highlight)
    {
        s_demo.highlight_timer -= dt;
        if (s_demo.highlight_timer <= 0.0f)
            s_demo.has_highlight = false;
    }

    if (s_demo.show_search_path)
    {
        s_demo.search_path_timer -= dt;
        if (s_demo.search_path_timer <= 0.0f)
            s_demo.show_search_path = false;
    }

    mel_render_list_clear(&s_sprite_list);

    i32 win_w, win_h;
    mel_window_size_pixels(s_window_handle, &win_w, &win_h);

    skiplist_draw(&s_demo, &s_sprite_list, mel_font_pool(), s_font_handle,
        (f32)win_w, (f32)win_h);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
        return;
    }

    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return;

    SkipListDemo* demo = &s_demo;

    if (demo->input_len < 15)
    {
        if (event->key.scancode >= SDL_SCANCODE_1 && event->key.scancode <= SDL_SCANCODE_9)
        {
            demo->input_buf[demo->input_len] = (char)('1' + (event->key.scancode - SDL_SCANCODE_1));
            demo->input_len++;
            return;
        }
        if (event->key.scancode == SDL_SCANCODE_0)
        {
            demo->input_buf[demo->input_len] = '0';
            demo->input_len++;
            return;
        }
        if (event->key.scancode >= SDL_SCANCODE_KP_1 && event->key.scancode <= SDL_SCANCODE_KP_9)
        {
            demo->input_buf[demo->input_len] = (char)('1' + (event->key.scancode - SDL_SCANCODE_KP_1));
            demo->input_len++;
            return;
        }
        if (event->key.scancode == SDL_SCANCODE_KP_0)
        {
            demo->input_buf[demo->input_len] = '0';
            demo->input_len++;
            return;
        }
    }

    switch (event->key.scancode)
    {
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        {
            i32 val = parse_input(demo);
            if (val >= 0)
            {
                demo_insert(demo, val);
                clear_input(demo);
            }
            break;
        }

        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE:
        {
            i32 val = parse_input(demo);
            if (val >= 0)
            {
                demo_remove(demo, val);
                clear_input(demo);
            }
            break;
        }

        case SDL_SCANCODE_F:
        {
            i32 val = parse_input(demo);
            if (val >= 0)
            {
                demo_find(demo, val);
                clear_input(demo);
            }
            break;
        }

        case SDL_SCANCODE_SPACE:
        {
            i32 val = (i32)(demo_rand(&demo->rng) % 99) + 1;
            demo_insert(demo, val);
            clear_input(demo);
            break;
        }

        case SDL_SCANCODE_I:
        {
            for (i32 i = 0; i < 10; i++)
            {
                i32 val = (i32)(demo_rand(&demo->rng) % 99) + 1;
                mel_skiplist_insert(&demo->list, (void*)(intptr_t)val, nullptr);
            }
            demo->last_op = OP_INSERT;
            demo->last_op_key = -1;
            demo->has_highlight = false;
            clear_input(demo);
            break;
        }

        case SDL_SCANCODE_S:
        {
            mel_skiplist_clear(&demo->list);
            demo->seed_counter++;
            mel_skiplist_seed(&demo->list, demo->seed_counter);
            demo->rng = SDL_GetTicks();
            if (demo->rng == 0) demo->rng = 42;
            demo->last_op = OP_SEED;
            demo->has_highlight = false;
            demo->show_search_path = false;
            clear_input(demo);
            break;
        }

        case SDL_SCANCODE_C:
        {
            mel_skiplist_clear(&demo->list);
            demo->last_op = OP_CLEAR;
            demo->has_highlight = false;
            demo->show_search_path = false;
            clear_input(demo);
            break;
        }

        default: break;
    }
}
