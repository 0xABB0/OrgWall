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
#include "collection.trie.h"
#include "allocator.heap.h"

#include <string.h>
#include <stdio.h>

#define MAX_RESULTS 128
#define MAX_WORD_LEN 64
#define MAX_VISIBLE_RESULTS 20
#define TRIE_VIS_MAX_DEPTH 4

static const char* DICTIONARY[] = {
    "apple", "app", "application", "apply", "art", "artist", "artificial",
    "ban", "band", "bank", "bar", "base", "bat", "batch",
    "bear", "beat", "beautiful", "begin", "between", "big", "bird",
    "bit", "black", "blue", "board", "boat", "book", "box",
    "break", "bridge", "bring", "build", "bus", "but", "buy",
    "call", "came", "can", "car", "care", "carry", "case",
    "cat", "catch", "change", "child", "city", "class", "clean",
    "clear", "close", "code", "cold", "come", "common", "complete",
    "computer", "condition", "contain", "control", "cool", "copy", "could",
    "count", "course", "cover", "create", "cross", "cut",
    "dark", "data", "day", "deal", "deep", "delete", "design",
    "develop", "did", "different", "direct", "disk", "do", "dog",
    "done", "door", "down", "draw", "drive", "drop", "dry", "during",
    "each", "early", "east", "easy", "edit", "end", "engine",
    "enter", "even", "event", "ever", "every",
};

#define DICTIONARY_COUNT (sizeof(DICTIONARY) / sizeof(DICTIONARY[0]))

typedef struct {
    Mel_Trie trie;
    char input_buf[MAX_WORD_LEN];
    i32 input_len;
    char results[MAX_RESULTS][MAX_WORD_LEN];
    i32 result_count;
    i32 scroll_offset;
    i32 word_count;
    f32 cursor_blink;
} TrieDemo;

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
static TrieDemo s_demo;

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

static Mel_TrieNode* find_prefix_node(Mel_Trie* trie, const char* prefix, i32 prefix_len)
{
    Mel_TrieNode* node = trie->root;
    for (i32 i = 0; i < prefix_len; i++)
    {
        u8 c = (u8)prefix[i];
        Mel_TrieNode* next = NULL;
        for (u32 j = 0; j < node->child_count; j++)
        {
            if (node->child_keys[j] == c)
            {
                next = node->children[j];
                break;
            }
        }
        if (!next) return NULL;
        node = next;
    }
    return node;
}

static void collect_words(Mel_TrieNode* node, char* buf, i32 depth, TrieDemo* demo)
{
    if (demo->result_count >= MAX_RESULTS) return;

    if (node->has_value)
    {
        buf[depth] = '\0';
        memcpy(demo->results[demo->result_count], buf, depth + 1);
        demo->result_count++;
    }

    for (u32 i = 0; i < node->child_count; i++)
    {
        if (depth + 1 < MAX_WORD_LEN)
        {
            buf[depth] = (char)node->child_keys[i];
            collect_words(node->children[i], buf, depth + 1, demo);
        }
    }
}

static void update_results(TrieDemo* demo)
{
    demo->result_count = 0;
    demo->scroll_offset = 0;

    if (demo->input_len == 0)
        return;

    Mel_TrieNode* prefix_node = find_prefix_node(&demo->trie, demo->input_buf, demo->input_len);
    if (!prefix_node) return;

    char buf[MAX_WORD_LEN];
    memcpy(buf, demo->input_buf, demo->input_len);
    collect_words(prefix_node, buf, demo->input_len, demo);
}

static void demo_load_dictionary(TrieDemo* demo)
{
    mel_trie_clear(&demo->trie);
    for (i32 i = 0; i < (i32)DICTIONARY_COUNT; i++)
        mel_trie_insert_str(&demo->trie, DICTIONARY[i], NULL);
    demo->word_count = (i32)mel_trie_count(&demo->trie);
}

static void demo_init(TrieDemo* demo)
{
    memset(demo, 0, sizeof(*demo));
    mel_trie_init(&demo->trie, mel_alloc_heap());
    demo_load_dictionary(demo);
}

static void draw_trie_node(Mel_TrieNode* node, u8 edge_key, i32 depth,
                           f32 x_min, f32 x_max, f32 y,
                           const char* prefix, i32 prefix_len,
                           Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe,
                           Mel_Gpu_Device* dev, bool on_path)
{
    if (depth >= TRIE_VIS_MAX_DEPTH) return;

    f32 cx = (x_min + x_max) * 0.5f;
    f32 node_size = 24.0f;

    bool is_on_path = on_path && depth <= prefix_len;

    Mel_Vec4 color = is_on_path
        ? mel_vec4(0.2f, 0.6f, 0.2f, 1.0f)
        : mel_vec4(0.2f, 0.25f, 0.35f, 1.0f);

    mel_sprite_batch_set_texture(batch, &s_white_texture);
    mel_sprite_batch_draw(batch, cx - node_size / 2, y, node_size, node_size, color);

    if (node->has_value)
    {
        mel_sprite_batch_draw(batch, cx - 2, y + node_size - 6, 4, 4,
            mel_vec4(0.2f, 0.9f, 0.2f, 1.0f));
    }

    char label[2] = { 0, 0 };
    if (depth == 0)
        label[0] = '.';
    else
        label[0] = (char)edge_key;

    Mel_Vec4 label_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec2 label_size = mel_font_atlas_measure_text(fe, str8_from_cstr(label));
    mel_sprite_batch_set_texture(batch, &fe->atlas_texture);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(label),
        cx - label_size.x / 2, y + (node_size - label_size.y) / 2, label_color);

    if (node->child_count > 0)
    {
        f32 child_width = (x_max - x_min) / (f32)node->child_count;
        f32 child_y = y + 50.0f;

        for (u32 i = 0; i < node->child_count; i++)
        {
            f32 child_x_min = x_min + child_width * (f32)i;
            f32 child_x_max = child_x_min + child_width;
            f32 child_cx = (child_x_min + child_x_max) * 0.5f;

            bool child_on_path = is_on_path && depth < prefix_len &&
                                  node->child_keys[i] == (u8)prefix[depth];

            Mel_Vec4 edge_color = child_on_path
                ? mel_vec4(0.3f, 0.8f, 0.3f, 0.8f)
                : mel_vec4(0.3f, 0.35f, 0.45f, 0.6f);

            mel_sprite_batch_set_texture(batch, &s_white_texture);

            f32 mid_y = y + node_size + (child_y - y - node_size) * 0.5f;
            mel_sprite_batch_draw(batch, cx, y + node_size, 1.0f, mid_y - (y + node_size), edge_color);

            f32 line_x = cx < child_cx ? cx : child_cx;
            f32 line_w = cx < child_cx ? child_cx - cx : cx - child_cx;
            if (line_w < 1.0f) line_w = 1.0f;
            mel_sprite_batch_draw(batch, line_x, mid_y, line_w, 1.0f, edge_color);

            mel_sprite_batch_draw(batch, child_cx, mid_y, 1.0f, child_y - mid_y, edge_color);

            draw_trie_node(node->children[i], node->child_keys[i], depth + 1,
                          child_x_min, child_x_max, child_y,
                          prefix, prefix_len,
                          batch, fe, dev, child_on_path);
        }
    }
}

static void draw_left_panel(TrieDemo* demo, Mel_SpriteBatch* batch,
                            Mel_Font_Atlas_Entry* fe, Mel_Gpu_Device* dev,
                            f32 panel_w, f32 panel_h)
{
    Mel_Vec4 bg = mel_vec4(0.1f, 0.1f, 0.13f, 1.0f);
    Mel_Vec4 input_bg = mel_vec4(0.15f, 0.15f, 0.2f, 1.0f);
    Mel_Vec4 input_border = mel_vec4(0.3f, 0.35f, 0.45f, 1.0f);
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 green = mel_vec4(0.3f, 0.9f, 0.3f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.5f, 0.5f, 0.5f, 1.0f);

    f32 pad = 15.0f;
    f32 input_x = pad;
    f32 input_y = pad;
    f32 input_w = panel_w - pad * 2;
    f32 input_h = 30.0f;

    mel_sprite_batch_set_texture(batch, &s_white_texture);
    mel_sprite_batch_draw(batch, 0, 0, panel_w, panel_h, bg);

    mel_sprite_batch_draw(batch, input_x - 1, input_y - 1, input_w + 2, input_h + 2, input_border);
    mel_sprite_batch_draw(batch, input_x, input_y, input_w, input_h, input_bg);

    mel_sprite_batch_set_texture(batch, &fe->atlas_texture);

    if (demo->input_len > 0)
    {
        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(demo->input_buf),
            input_x + 6, input_y + 6, white);
    }
    else
    {
        mel_font_atlas_draw_text(fe, batch, S8("type to search..."),
            input_x + 6, input_y + 6, dim);
    }

    f32 cursor_x = input_x + 6;
    if (demo->input_len > 0)
    {
        Mel_Vec2 text_size = mel_font_atlas_measure_text(fe, str8_from_cstr(demo->input_buf));
        cursor_x += text_size.x;
    }

    if (demo->cursor_blink < 0.5f)
    {
        mel_sprite_batch_set_texture(batch, &s_white_texture);
        mel_sprite_batch_draw(batch, cursor_x, input_y + 5, 2.0f, 18.0f, white);
    }

    f32 results_y = input_y + input_h + 15.0f;
    f32 line_height = 22.0f;

    mel_sprite_batch_set_texture(batch, &fe->atlas_texture);

    i32 visible_count = MAX_VISIBLE_RESULTS;
    if (demo->result_count - demo->scroll_offset < visible_count)
        visible_count = demo->result_count - demo->scroll_offset;

    for (i32 i = 0; i < visible_count; i++)
    {
        i32 idx = demo->scroll_offset + i;
        const char* result = demo->results[idx];
        f32 ry = results_y + (f32)i * line_height;

        char prefix_str[MAX_WORD_LEN];
        char suffix_str[MAX_WORD_LEN];
        i32 plen = demo->input_len;
        i32 rlen = (i32)strlen(result);

        if (plen > rlen) plen = rlen;
        memcpy(prefix_str, result, plen);
        prefix_str[plen] = '\0';
        strcpy(suffix_str, result + plen);

        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(prefix_str),
            input_x + 6, ry, green);

        if (suffix_str[0] != '\0')
        {
            Mel_Vec2 prefix_size = mel_font_atlas_measure_text(fe, str8_from_cstr(prefix_str));
            mel_font_atlas_draw_text(fe, batch, str8_from_cstr(suffix_str),
                input_x + 6 + prefix_size.x, ry, white);
        }
    }

    if (demo->result_count > MAX_VISIBLE_RESULTS)
    {
        char scroll_buf[64];
        snprintf(scroll_buf, sizeof(scroll_buf), "(%d-%d of %d)",
            demo->scroll_offset + 1,
            demo->scroll_offset + visible_count,
            demo->result_count);
        f32 scroll_y = results_y + (f32)MAX_VISIBLE_RESULTS * line_height + 5.0f;
        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(scroll_buf),
            input_x + 6, scroll_y, dim);
    }

    char word_buf[64];
    snprintf(word_buf, sizeof(word_buf), "Words: %d", demo->word_count);
    f32 word_y = panel_h - 60.0f;
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(word_buf),
        input_x + 6, word_y, dim);

    mel_font_atlas_draw_text(fe, batch, S8("A-Z:type  BS:delete  TAB:complete"),
        input_x + 6, word_y + 20.0f, dim);
    mel_font_atlas_draw_text(fe, batch, S8("ENTER:add  DEL:remove  Shift+C:clear  ESC:quit"),
        input_x + 6, word_y + 38.0f, dim);
}

static void draw_right_panel(TrieDemo* demo, Mel_SpriteBatch* batch,
                             Mel_Font_Atlas_Entry* fe, Mel_Gpu_Device* dev,
                             f32 x_offset, f32 panel_w, f32 panel_h)
{
    Mel_Vec4 bg = mel_vec4(0.08f, 0.08f, 0.1f, 1.0f);

    mel_sprite_batch_set_texture(batch, &s_white_texture);
    mel_sprite_batch_draw(batch, x_offset, 0, panel_w, panel_h, bg);

    Mel_Vec4 title_color = mel_vec4(0.6f, 0.6f, 0.7f, 1.0f);
    mel_sprite_batch_set_texture(batch, &fe->atlas_texture);
    mel_font_atlas_draw_text(fe, batch, S8("Trie Structure"),
        x_offset + 10.0f, 10.0f, title_color);

    if (!demo->trie.root) return;

    f32 tree_x_min = x_offset + 10.0f;
    f32 tree_x_max = x_offset + panel_w - 10.0f;
    f32 tree_y = 40.0f;

    draw_trie_node(demo->trie.root, '.', 0,
                  tree_x_min, tree_x_max, tree_y,
                  demo->input_buf, demo->input_len,
                  batch, fe, dev, true);
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

    SDL_Log("Trie Autocomplete ready! Type to search, ESC to quit");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Trie Autocomplete", 1100, 700,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Trie"),
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

    mel_trie_free(&s_demo.trie);

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
    s_demo.cursor_blink += dt;
    if (s_demo.cursor_blink >= 1.0f)
        s_demo.cursor_blink -= 1.0f;
}

static void app_render(Mel_App* app, Mel_Gpu_Cmd* c)
{
    Mel_Engine* e = &app->engine;

    if (!s_pipeline.pipeline) return;

    mel_engine_begin_swapchain_pass(e, c,
        .clear_r = 0.08f, .clear_g = 0.08f, .clear_b = 0.1f, .clear_a = 1.0f);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                    0, (f32)e->swapchain.extent.height, -1, 1);

    f32 sw = (f32)e->swapchain.extent.width;
    f32 sh = (f32)e->swapchain.extent.height;
    f32 left_w = sw * 0.38f;
    f32 right_w = sw - left_w;

    mel_sprite_batch_begin(&s_batch, &s_pipeline);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
    {
        draw_left_panel(&s_demo, &s_batch, fe, &e->dev, left_w, sh);
        draw_right_panel(&s_demo, &s_batch, fe, &e->dev, left_w, right_w, sh);
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

    if (event->type == SDL_EVENT_TEXT_INPUT)
    {
        if (SDL_GetModState() & SDL_KMOD_SHIFT)
            return;

        const char* text = event->text.text;
        i32 len = (i32)strlen(text);
        for (i32 i = 0; i < len && s_demo.input_len < MAX_WORD_LEN - 1; i++)
        {
            char c = text[i];
            if (c >= 'a' && c <= 'z')
            {
                s_demo.input_buf[s_demo.input_len++] = c;
            }
            else if (c >= 'A' && c <= 'Z')
            {
                s_demo.input_buf[s_demo.input_len++] = (char)(c - 'A' + 'a');
            }
        }
        s_demo.input_buf[s_demo.input_len] = '\0';
        s_demo.cursor_blink = 0.0f;
        update_results(&s_demo);
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_BACKSPACE:
                if (s_demo.input_len > 0)
                {
                    s_demo.input_len--;
                    s_demo.input_buf[s_demo.input_len] = '\0';
                    s_demo.cursor_blink = 0.0f;
                    update_results(&s_demo);
                }
                break;

            case SDL_SCANCODE_RETURN:
                if (s_demo.input_len > 0)
                {
                    if (!mel_trie_contains_str(&s_demo.trie, s_demo.input_buf))
                    {
                        mel_trie_insert_str(&s_demo.trie, s_demo.input_buf, NULL);
                        s_demo.word_count = (i32)mel_trie_count(&s_demo.trie);
                        update_results(&s_demo);
                    }
                }
                break;

            case SDL_SCANCODE_DELETE:
                if (s_demo.result_count > 0)
                {
                    mel_trie_remove_str(&s_demo.trie, s_demo.results[0]);
                    s_demo.word_count = (i32)mel_trie_count(&s_demo.trie);
                    update_results(&s_demo);
                }
                break;

            case SDL_SCANCODE_TAB:
                if (s_demo.result_count > 0)
                {
                    i32 rlen = (i32)strlen(s_demo.results[0]);
                    memcpy(s_demo.input_buf, s_demo.results[0], rlen + 1);
                    s_demo.input_len = rlen;
                    s_demo.cursor_blink = 0.0f;
                    update_results(&s_demo);
                }
                break;

            case SDL_SCANCODE_C:
                if (SDL_GetModState() & SDL_KMOD_SHIFT)
                {
                    s_demo.input_len = 0;
                    s_demo.input_buf[0] = '\0';
                    demo_load_dictionary(&s_demo);
                    s_demo.result_count = 0;
                    s_demo.scroll_offset = 0;
                    s_demo.cursor_blink = 0.0f;
                }
                break;

            case SDL_SCANCODE_UP:
                if (s_demo.scroll_offset > 0)
                    s_demo.scroll_offset--;
                break;

            case SDL_SCANCODE_DOWN:
                if (s_demo.scroll_offset < s_demo.result_count - MAX_VISIBLE_RESULTS)
                    s_demo.scroll_offset++;
                break;

            default: break;
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat)
    {
        if (event->key.scancode == SDL_SCANCODE_BACKSPACE && s_demo.input_len > 0)
        {
            s_demo.input_len--;
            s_demo.input_buf[s_demo.input_len] = '\0';
            s_demo.cursor_blink = 0.0f;
            update_results(&s_demo);
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
