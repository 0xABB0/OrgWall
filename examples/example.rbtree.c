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
#include "sprite.pass.h"
#include "render.graph.h"
#include "render.list.h"
#include "render.target.h"
#include "render.camera.h"
#include "texture.pool.h"
#include "font.atlas.h"
#include "vfs.h"
#include "allocator.heap.h"
#include "sim.ctx.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec2.h"
#include "math.geo.rect.h"
#include "collection.rbtree.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define OP_NONE    0
#define OP_INSERT  1
#define OP_REMOVE  2
#define OP_FIND    3
#define OP_CLEAR   4

#define NODE_SIZE    24.0f
#define NODE_HSPACE  50.0f
#define NODE_VSPACE  60.0f
#define EDGE_THICK   2.0f
#define PANEL_WIDTH  250.0f

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Font_Handle s_font_handle;
static Mel_Render_List s_sprite_list;
static Mel_Render_List s_font_list;

typedef struct {
    f32 x, y;
    intptr_t key;
    u8 color;
} NodePos;

typedef struct {
    Mel_RBTree tree;
    NodePos positions[256];
    i32 position_count;
    char input_buf[16];
    i32 input_len;
    i32 highlighted_key;
    bool has_highlight;
    f32 highlight_timer;
    u32 last_op;
    i32 last_op_key;
    u64 rng;
} RBTreeDemo;

static RBTreeDemo s_demo;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
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

static void demo_init(RBTreeDemo* d)
{
    mel_rbtree_init(&d->tree, int_compare, mel_alloc_heap());
    d->position_count = 0;
    d->input_len = 0;
    memset(d->input_buf, 0, sizeof(d->input_buf));
    d->highlighted_key = 0;
    d->has_highlight = false;
    d->highlight_timer = 0.0f;
    d->last_op = OP_NONE;
    d->last_op_key = 0;
    d->rng = SDL_GetTicks();
    if (d->rng == 0) d->rng = 42;
}

static i32 input_value(RBTreeDemo* d)
{
    if (d->input_len == 0) return -1;
    i32 val = 0;
    for (i32 i = 0; i < d->input_len; i++)
        val = val * 10 + (d->input_buf[i] - '0');
    return val;
}

static void input_clear(RBTreeDemo* d)
{
    d->input_len = 0;
    memset(d->input_buf, 0, sizeof(d->input_buf));
}

static void demo_insert(RBTreeDemo* d, i32 val)
{
    mel_rbtree_insert(&d->tree, (void*)(intptr_t)val, (void*)(intptr_t)val);
    d->last_op = OP_INSERT;
    d->last_op_key = val;
}

static void demo_remove(RBTreeDemo* d, i32 val)
{
    mel_rbtree_remove(&d->tree, (void*)(intptr_t)val);
    d->last_op = OP_REMOVE;
    d->last_op_key = val;
}

static void demo_find(RBTreeDemo* d, i32 val)
{
    Mel_RBNode* node = mel_rbtree_find(&d->tree, (void*)(intptr_t)val);
    d->last_op = OP_FIND;
    d->last_op_key = val;
    if (node != &d->tree.nil_node)
    {
        d->has_highlight = true;
        d->highlighted_key = val;
        d->highlight_timer = 2.0f;
    }
}

static i32 layout_inorder(Mel_RBTree* tree, Mel_RBNode* node, NodePos* positions, i32 index, i32 depth)
{
    if (node == &tree->nil_node) return index;

    index = layout_inorder(tree, node->left, positions, index, depth + 1);

    positions[index].x = (f32)index * NODE_HSPACE;
    positions[index].y = (f32)depth * NODE_VSPACE;
    positions[index].key = (intptr_t)node->key;
    positions[index].color = node->color;
    index++;

    index = layout_inorder(tree, node->right, positions, index, depth + 1);

    return index;
}

static i32 find_pos_index(RBTreeDemo* d, intptr_t key)
{
    for (i32 i = 0; i < d->position_count; i++)
        if (d->positions[i].key == key) return i;
    return -1;
}

typedef struct {
    intptr_t parent_key;
    intptr_t child_key;
} EdgePair;

static i32 collect_edges(Mel_RBTree* tree, Mel_RBNode* node, EdgePair* edges, i32 count)
{
    if (node == &tree->nil_node) return count;

    if (node->left != &tree->nil_node)
    {
        edges[count].parent_key = (intptr_t)node->key;
        edges[count].child_key = (intptr_t)node->left->key;
        count++;
    }
    if (node->right != &tree->nil_node)
    {
        edges[count].parent_key = (intptr_t)node->key;
        edges[count].child_key = (intptr_t)node->right->key;
        count++;
    }

    count = collect_edges(tree, node->left, edges, count);
    count = collect_edges(tree, node->right, edges, count);
    return count;
}

static void push_rect(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    Mel_Sprite_Entry* e = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
    *e = (Mel_Sprite_Entry){
        .pos = mel_vec2(x, y), .size = mel_vec2(w, h),
        .uv = MEL_UV_FULL, .color = color,
    };
}

static void draw_edges(RBTreeDemo* d, Mel_Render_List* list, f32 ox, f32 oy)
{
    EdgePair edges[256];
    i32 edge_count = collect_edges(&d->tree, d->tree.root, edges, 0);
    Mel_Vec4 edge_color = mel_vec4(0.35f, 0.35f, 0.4f, 1.0f);

    for (i32 i = 0; i < edge_count; i++)
    {
        i32 pi = find_pos_index(d, edges[i].parent_key);
        i32 ci = find_pos_index(d, edges[i].child_key);
        if (pi < 0 || ci < 0) continue;

        f32 px = ox + d->positions[pi].x + NODE_SIZE * 0.5f;
        f32 py = oy + d->positions[pi].y + NODE_SIZE;
        f32 cx = ox + d->positions[ci].x + NODE_SIZE * 0.5f;
        f32 cy = oy + d->positions[ci].y;

        f32 vx = px;
        f32 vy_top = py;
        f32 vy_bot = cy;
        f32 v_height = vy_bot - vy_top;
        if (v_height > 0)
            push_rect(list, vx - EDGE_THICK * 0.5f, vy_top, EDGE_THICK, v_height, edge_color);

        f32 hx_left = px < cx ? px : cx;
        f32 hx_right = px > cx ? px : cx;
        f32 h_width = hx_right - hx_left;
        if (h_width > 0)
            push_rect(list, hx_left, cy - EDGE_THICK * 0.5f, h_width, EDGE_THICK, edge_color);
    }
}

static void draw_nodes(RBTreeDemo* d, Mel_Render_List* list, f32 ox, f32 oy)
{
    for (i32 i = 0; i < d->position_count; i++)
    {
        f32 nx = ox + d->positions[i].x;
        f32 ny = oy + d->positions[i].y;
        intptr_t key = d->positions[i].key;

        if (d->has_highlight && d->highlighted_key == (i32)key)
        {
            Mel_Vec4 yellow = mel_vec4(0.95f, 0.9f, 0.1f, 1.0f);
            push_rect(list, nx - 3.0f, ny - 3.0f, NODE_SIZE + 6.0f, NODE_SIZE + 6.0f, yellow);
        }

        Mel_Vec4 node_color;
        if (d->positions[i].color == MEL_RB_RED)
            node_color = mel_vec4(0.85f, 0.2f, 0.2f, 1.0f);
        else
            node_color = mel_vec4(0.25f, 0.25f, 0.25f, 1.0f);

        push_rect(list, nx, ny, NODE_SIZE, NODE_SIZE, node_color);
    }
}

static void draw_node_labels(RBTreeDemo* d, Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font, Mel_Render_List* list, f32 ox, f32 oy)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);

    for (i32 i = 0; i < d->position_count; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)d->positions[i].key);

        Mel_Vec2 text_size = mel_font_atlas_measure_text(pool, font, str8_from_cstr(buf));
        f32 nx = ox + d->positions[i].x + NODE_SIZE * 0.5f - text_size.x * 0.5f;
        f32 ny = oy + d->positions[i].y + NODE_SIZE * 0.5f - text_size.y * 0.5f;

        mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), nx, ny, white);
    }
}

static void draw_panel(Mel_Render_List* list, f32 panel_x, f32 win_h)
{
    Mel_Vec4 panel_bg = mel_vec4(0.06f, 0.06f, 0.08f, 1.0f);
    push_rect(list, panel_x, 0, PANEL_WIDTH, win_h, panel_bg);

    Mel_Vec4 border = mel_vec4(0.2f, 0.2f, 0.25f, 1.0f);
    push_rect(list, panel_x, 0, 2.0f, win_h, border);
}

static void draw_panel_text(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font, Mel_Render_List* list, f32 panel_x, RBTreeDemo* d)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);
    Mel_Vec4 green = mel_vec4(0.4f, 0.9f, 0.4f, 1.0f);

    f32 tx = panel_x + 16.0f;
    f32 ty = 20.0f;

    mel_font_atlas_draw_text(pool, font, list, S8("RED-BLACK TREE"), tx, ty, white);
    ty += 40.0f;

    char buf[64];
    snprintf(buf, sizeof(buf), "Nodes: %d", (int)mel_rbtree_count(&d->tree));
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), tx, ty, green);
    ty += 30.0f;

    if (d->last_op != OP_NONE)
    {
        const char* op_str = "";
        if (d->last_op == OP_INSERT) op_str = "Inserted";
        else if (d->last_op == OP_REMOVE) op_str = "Removed";
        else if (d->last_op == OP_FIND) op_str = "Found";
        else if (d->last_op == OP_CLEAR) op_str = "Cleared";

        if (d->last_op == OP_CLEAR)
            snprintf(buf, sizeof(buf), "Last: %s", op_str);
        else
            snprintf(buf, sizeof(buf), "Last: %s %d", op_str, d->last_op_key);

        mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), tx, ty, dim);
        ty += 30.0f;
    }

    ty += 10.0f;
    char input_display[32];
    snprintf(input_display, sizeof(input_display), "Input: %.*s_", d->input_len, d->input_buf);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(input_display), tx, ty, white);
    ty += 40.0f;

    Mel_Vec4 ctrl = mel_vec4(0.5f, 0.5f, 0.55f, 1.0f);
    mel_font_atlas_draw_text(pool, font, list, S8("CONTROLS"), tx, ty, white);
    ty += 28.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("0-9    Type number"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("Enter  Insert"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("Del    Remove"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("F      Find"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("Bksp   Delete char"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("Space  Random insert"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("I      Insert 7 rand"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("C      Clear tree"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("Esc    Quit"), tx, ty, ctrl);
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    s_font_handle = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    demo_init(&s_demo);

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&s_font_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_target_init_swapchain(&s_swapchain_target, sc, dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent.width,
                                      0, (f32)sc->extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&s_graph, S8("sprite"),
        .fn = mel_sprite_pass_execute,
        .user = mel_sprite_pass(),
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

void app_init(void)
{
    mel_init(.app_name = S8("Melody RBTree"), .enable_validation = true);
    s_window_handle = mel_window_create(S8("Melody Red-Black Tree"), .width = 1000, .height = 700);
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

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    mel_rbtree_free(&s_demo.tree);
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
        {
            s_demo.has_highlight = false;
            s_demo.highlight_timer = 0.0f;
        }
    }

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_font_list);

    i32 ww, wh;
    mel_window_size(s_window_handle, &ww, &wh);
    f32 win_w = (f32)ww;
    f32 win_h = (f32)wh;

    f32 panel_x = win_w - PANEL_WIDTH;
    f32 tree_area_w = panel_x;

    Mel_Vec4 tree_bg = mel_vec4(0.05f, 0.05f, 0.07f, 1.0f);
    push_rect(&s_sprite_list, 0, 0, tree_area_w, win_h, tree_bg);

    draw_panel(&s_sprite_list, panel_x, win_h);

    s_demo.position_count = 0;
    if (s_demo.tree.root != &s_demo.tree.nil_node)
    {
        s_demo.position_count = layout_inorder(&s_demo.tree, s_demo.tree.root, s_demo.positions, 0, 0);

        f32 min_x = s_demo.positions[0].x;
        f32 max_x = s_demo.positions[s_demo.position_count - 1].x + NODE_SIZE;
        f32 tree_w = max_x - min_x;

        f32 max_y = 0;
        for (i32 i = 0; i < s_demo.position_count; i++)
            if (s_demo.positions[i].y > max_y)
                max_y = s_demo.positions[i].y;
        f32 tree_h = max_y + NODE_SIZE;

        f32 ox = (tree_area_w - tree_w) * 0.5f - min_x;
        f32 oy = 40.0f;

        if (tree_h + oy > win_h - 20.0f)
            oy = 20.0f;

        draw_edges(&s_demo, &s_sprite_list, ox, oy);
        draw_nodes(&s_demo, &s_sprite_list, ox, oy);

        draw_node_labels(&s_demo, mel_font_pool(), s_font_handle, &s_font_list, ox, oy);
    }

    draw_panel_text(mel_font_pool(), s_font_handle, &s_font_list, panel_x, &s_demo);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
        return;
    }

    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) return;

    SDL_Scancode sc = event->key.scancode;

    if (s_demo.input_len < 15)
    {
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        {
            s_demo.input_buf[s_demo.input_len] = (char)('1' + (sc - SDL_SCANCODE_1));
            s_demo.input_len++;
            return;
        }
        if (sc == SDL_SCANCODE_0)
        {
            s_demo.input_buf[s_demo.input_len] = '0';
            s_demo.input_len++;
            return;
        }
        if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
        {
            s_demo.input_buf[s_demo.input_len] = (char)('1' + (sc - SDL_SCANCODE_KP_1));
            s_demo.input_len++;
            return;
        }
        if (sc == SDL_SCANCODE_KP_0)
        {
            s_demo.input_buf[s_demo.input_len] = '0';
            s_demo.input_len++;
            return;
        }
    }

    if (sc == SDL_SCANCODE_BACKSPACE)
    {
        if (s_demo.input_len > 0)
        {
            s_demo.input_len--;
            s_demo.input_buf[s_demo.input_len] = 0;
        }
        return;
    }

    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
    {
        i32 val = input_value(&s_demo);
        if (val >= 0)
        {
            demo_insert(&s_demo, val);
            input_clear(&s_demo);
        }
        return;
    }

    if (sc == SDL_SCANCODE_DELETE)
    {
        i32 val = input_value(&s_demo);
        if (val >= 0)
        {
            demo_remove(&s_demo, val);
            input_clear(&s_demo);
        }
        return;
    }

    if (sc == SDL_SCANCODE_F)
    {
        i32 val = input_value(&s_demo);
        if (val >= 0)
        {
            demo_find(&s_demo, val);
            input_clear(&s_demo);
        }
        return;
    }

    if (sc == SDL_SCANCODE_C)
    {
        mel_rbtree_clear(&s_demo.tree);
        s_demo.last_op = OP_CLEAR;
        s_demo.last_op_key = 0;
        s_demo.has_highlight = false;
        input_clear(&s_demo);
        return;
    }

    if (sc == SDL_SCANCODE_SPACE)
    {
        i32 val = (i32)(demo_rand(&s_demo.rng) % 99) + 1;
        demo_insert(&s_demo, val);
        return;
    }

    if (sc == SDL_SCANCODE_I)
    {
        for (i32 i = 0; i < 7; i++)
        {
            i32 val = (i32)(demo_rand(&s_demo.rng) % 99) + 1;
            demo_insert(&s_demo, val);
        }
        return;
    }
}
