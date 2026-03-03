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
#include "vfs.h"
#include "vfs.backend.os.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "collection.rbtree.h"
#include "allocator.heap.h"

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

static SDL_Window* s_window;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_pipeline;
static Mel_Gpu_Texture s_white_texture;
static Mel_SpriteBatch s_batch;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;

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

static void draw_edges(RBTreeDemo* d, Mel_SpriteBatch* batch, f32 ox, f32 oy)
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
            mel_sprite_batch_draw(batch, vx - EDGE_THICK * 0.5f, vy_top, EDGE_THICK, v_height, edge_color);

        f32 hx_left = px < cx ? px : cx;
        f32 hx_right = px > cx ? px : cx;
        f32 h_width = hx_right - hx_left;
        if (h_width > 0)
            mel_sprite_batch_draw(batch, hx_left, cy - EDGE_THICK * 0.5f, h_width, EDGE_THICK, edge_color);
    }
}

static void draw_nodes(RBTreeDemo* d, Mel_SpriteBatch* batch, f32 ox, f32 oy)
{
    for (i32 i = 0; i < d->position_count; i++)
    {
        f32 nx = ox + d->positions[i].x;
        f32 ny = oy + d->positions[i].y;
        intptr_t key = d->positions[i].key;

        if (d->has_highlight && d->highlighted_key == (i32)key)
        {
            Mel_Vec4 yellow = mel_vec4(0.95f, 0.9f, 0.1f, 1.0f);
            mel_sprite_batch_draw(batch, nx - 3.0f, ny - 3.0f, NODE_SIZE + 6.0f, NODE_SIZE + 6.0f, yellow);
        }

        Mel_Vec4 node_color;
        if (d->positions[i].color == MEL_RB_RED)
            node_color = mel_vec4(0.85f, 0.2f, 0.2f, 1.0f);
        else
            node_color = mel_vec4(0.25f, 0.25f, 0.25f, 1.0f);

        mel_sprite_batch_draw(batch, nx, ny, NODE_SIZE, NODE_SIZE, node_color);
    }
}

static void draw_node_labels(RBTreeDemo* d, Mel_Font_Atlas_Entry* fe, Mel_SpriteBatch* batch, f32 ox, f32 oy)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);

    for (i32 i = 0; i < d->position_count; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)d->positions[i].key);

        Mel_Vec2 text_size = mel_font_atlas_measure_text(fe, str8_from_cstr(buf));
        f32 nx = ox + d->positions[i].x + NODE_SIZE * 0.5f - text_size.x * 0.5f;
        f32 ny = oy + d->positions[i].y + NODE_SIZE * 0.5f - text_size.y * 0.5f;

        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), nx, ny, white);
    }
}

static void draw_panel(Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe, f32 panel_x, f32 win_h, RBTreeDemo* d)
{
    Mel_Vec4 panel_bg = mel_vec4(0.06f, 0.06f, 0.08f, 1.0f);
    mel_sprite_batch_draw(batch, panel_x, 0, PANEL_WIDTH, win_h, panel_bg);

    Mel_Vec4 border = mel_vec4(0.2f, 0.2f, 0.25f, 1.0f);
    mel_sprite_batch_draw(batch, panel_x, 0, 2.0f, win_h, border);
}

static void draw_panel_text(Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe, f32 panel_x, RBTreeDemo* d)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);
    Mel_Vec4 green = mel_vec4(0.4f, 0.9f, 0.4f, 1.0f);

    f32 tx = panel_x + 16.0f;
    f32 ty = 20.0f;

    mel_font_atlas_draw_text(fe, batch, S8("RED-BLACK TREE"), tx, ty, white);
    ty += 40.0f;

    char buf[64];
    snprintf(buf, sizeof(buf), "Nodes: %d", (int)mel_rbtree_count(&d->tree));
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), tx, ty, green);
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

        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), tx, ty, dim);
        ty += 30.0f;
    }

    ty += 10.0f;
    char input_display[32];
    snprintf(input_display, sizeof(input_display), "Input: %.*s_", d->input_len, d->input_buf);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(input_display), tx, ty, white);
    ty += 40.0f;

    Mel_Vec4 ctrl = mel_vec4(0.5f, 0.5f, 0.55f, 1.0f);
    mel_font_atlas_draw_text(fe, batch, S8("CONTROLS"), tx, ty, white);
    ty += 28.0f;
    mel_font_atlas_draw_text(fe, batch, S8("0-9    Type number"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("Enter  Insert"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("Del    Remove"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("F      Find"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("Bksp   Delete char"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("Space  Random insert"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("I      Insert 7 rand"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("C      Clear tree"), tx, ty, ctrl);
    ty += 22.0f;
    mel_font_atlas_draw_text(fe, batch, S8("Esc    Quit"), tx, ty, ctrl);
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

    mel_font_atlas_pool_init(&s_font_pool, &e->allocator, dev, &s_demo_vfs);
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
    s_window = SDL_CreateWindow("Melody Red-Black Tree", 1000, 700,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody RBTree"),
        .enable_validation = true,
        .enable_imgui = false,
        .fixed_dt = 1.0f / 60.0f);

    Mel_Io_Desc io_desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&s_demo_io, &io_desc);
    Mel_Vfs_Desc vfs_desc = { .allocator = mel_alloc_heap(), .io = &s_demo_io };
    mel_vfs_init(&s_demo_vfs, &vfs_desc);
    s_fonts_backend = mel_vfs_backend_os_create(mel_alloc_heap(), S8("/"));
    mel_vfs_mount(&s_demo_vfs, S8("/"), s_fonts_backend, 0, false);

    on_init(&app->engine);
}

static void app_shutdown(Mel_App* app)
{
    Mel_Gpu_Device* dev = &app->engine.dev;
    mel_gpu_device_wait_idle(dev);

    mel_rbtree_free(&s_demo.tree);
    mel_sprite_batch_shutdown(&s_batch, dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
    mel_vfs_backend_os_destroy(s_fonts_backend);
    mel_gpu_texture_shutdown(&s_white_texture, dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, dev);
    mel_gpu_shader_shutdown(&s_shader, dev);
}

static void app_update(Mel_App* app, f32 dt)
{
    MEL_UNUSED(app);

    if (s_demo.has_highlight)
    {
        s_demo.highlight_timer -= dt;
        if (s_demo.highlight_timer <= 0.0f)
        {
            s_demo.has_highlight = false;
            s_demo.highlight_timer = 0.0f;
        }
    }
}

static void app_render(Mel_App* app, Mel_Gpu_Cmd* c)
{
    Mel_Engine* e = &app->engine;

    if (!s_pipeline.pipeline) return;

    mel_engine_begin_swapchain_pass(e, c,
        .clear_r = 0.08f, .clear_g = 0.08f, .clear_b = 0.1f, .clear_a = 1.0f);

    f32 win_w = (f32)e->swapchain.extent.width;
    f32 win_h = (f32)e->swapchain.extent.height;
    Mel_Mat4 proj = mel_mat4_ortho(0, win_w, 0, win_h, -1, 1);

    f32 panel_x = win_w - PANEL_WIDTH;
    f32 tree_area_w = panel_x;

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &s_white_texture);

    Mel_Vec4 tree_bg = mel_vec4(0.05f, 0.05f, 0.07f, 1.0f);
    mel_sprite_batch_draw(&s_batch, 0, 0, tree_area_w, win_h, tree_bg);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);

    draw_panel(&s_batch, fe, panel_x, win_h, &s_demo);

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

        draw_edges(&s_demo, &s_batch, ox, oy);
        draw_nodes(&s_demo, &s_batch, ox, oy);

        if (fe)
            draw_node_labels(&s_demo, fe, &s_batch, ox, oy);
    }

    if (fe)
        draw_panel_text(&s_batch, fe, panel_x, &s_demo);

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

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_render = app_render,
    .on_event = app_event
)
