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
#include "font.desc.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "collection.trie.h"
#include "sim.ctx.h"

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

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Font_Atlas_Handle s_font_handle;
static TrieDemo s_demo;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Render_List s_sprite_list;
static Mel_Render_List s_font_list;

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

static void push_rect(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_draw_sprite(list, .pos = mel_vec2(x, y), .size = mel_vec2(w, h), .color = color);
}

static void draw_trie_node(Mel_TrieNode* node, u8 edge_key, i32 depth,
                           f32 x_min, f32 x_max, f32 y,
                           const char* prefix, i32 prefix_len,
                           Mel_Render_List* list, Mel_Render_List* font_list,
                           Mel_Font_Atlas_Handle font, bool on_path)
{
    if (depth >= TRIE_VIS_MAX_DEPTH) return;

    f32 cx = (x_min + x_max) * 0.5f;
    f32 node_size = 24.0f;

    bool is_on_path = on_path && depth <= prefix_len;

    Mel_Vec4 color = is_on_path
        ? mel_vec4(0.2f, 0.6f, 0.2f, 1.0f)
        : mel_vec4(0.2f, 0.25f, 0.35f, 1.0f);

    push_rect(list, cx - node_size / 2, y, node_size, node_size, color);

    if (node->has_value)
        push_rect(list, cx - 2, y + node_size - 6, 4, 4, mel_vec4(0.2f, 0.9f, 0.2f, 1.0f));

    char label[2] = { 0, 0 };
    if (depth == 0)
        label[0] = '.';
    else
        label[0] = (char)edge_key;

    Mel_Vec4 label_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec2 label_size = mel_font_atlas_measure_text(font, str8_from_cstr(label));
    mel_font_atlas_draw_text(font, font_list, str8_from_cstr(label),
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

            f32 mid_y = y + node_size + (child_y - y - node_size) * 0.5f;
            push_rect(list, cx, y + node_size, 1.0f, mid_y - (y + node_size), edge_color);

            f32 line_x = cx < child_cx ? cx : child_cx;
            f32 line_w = cx < child_cx ? child_cx - cx : cx - child_cx;
            if (line_w < 1.0f) line_w = 1.0f;
            push_rect(list, line_x, mid_y, line_w, 1.0f, edge_color);

            push_rect(list, child_cx, mid_y, 1.0f, child_y - mid_y, edge_color);

            draw_trie_node(node->children[i], node->child_keys[i], depth + 1,
                          child_x_min, child_x_max, child_y,
                          prefix, prefix_len,
                          list, font_list, font, child_on_path);
        }
    }
}

static void draw_left_panel(TrieDemo* demo, Mel_Render_List* list,
                            Mel_Render_List* font_list,
                            Mel_Font_Atlas_Handle font, f32 panel_w, f32 panel_h)
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

    push_rect(list, 0, 0, panel_w, panel_h, bg);
    push_rect(list, input_x - 1, input_y - 1, input_w + 2, input_h + 2, input_border);
    push_rect(list, input_x, input_y, input_w, input_h, input_bg);

    if (demo->input_len > 0)
    {
        mel_font_atlas_draw_text(font, font_list, str8_from_cstr(demo->input_buf),
            input_x + 6, input_y + 6, white);
    }
    else
    {
        mel_font_atlas_draw_text(font, font_list, S8("type to search..."),
            input_x + 6, input_y + 6, dim);
    }

    f32 cursor_x = input_x + 6;
    if (demo->input_len > 0)
    {
        Mel_Vec2 text_size = mel_font_atlas_measure_text(font, str8_from_cstr(demo->input_buf));
        cursor_x += text_size.x;
    }

    if (demo->cursor_blink < 0.5f)
        push_rect(list, cursor_x, input_y + 5, 2.0f, 18.0f, white);

    f32 results_y = input_y + input_h + 15.0f;
    f32 line_height = 22.0f;

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

        mel_font_atlas_draw_text(font, font_list, str8_from_cstr(prefix_str),
            input_x + 6, ry, green);

        if (suffix_str[0] != '\0')
        {
            Mel_Vec2 prefix_size = mel_font_atlas_measure_text(font, str8_from_cstr(prefix_str));
            mel_font_atlas_draw_text(font, font_list, str8_from_cstr(suffix_str),
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
        mel_font_atlas_draw_text(font, font_list, str8_from_cstr(scroll_buf),
            input_x + 6, scroll_y, dim);
    }

    char word_buf[64];
    snprintf(word_buf, sizeof(word_buf), "Words: %d", demo->word_count);
    f32 word_y = panel_h - 60.0f;
    mel_font_atlas_draw_text(font, font_list, str8_from_cstr(word_buf),
        input_x + 6, word_y, dim);

    mel_font_atlas_draw_text(font, font_list, S8("A-Z:type  BS:delete  TAB:complete"),
        input_x + 6, word_y + 20.0f, dim);
    mel_font_atlas_draw_text(font, font_list, S8("ENTER:add  DEL:remove  Shift+C:clear  ESC:quit"),
        input_x + 6, word_y + 38.0f, dim);
}

static void draw_right_panel(TrieDemo* demo, Mel_Render_List* list,
                             Mel_Render_List* font_list,
                             Mel_Font_Atlas_Handle font, f32 x_offset, f32 panel_w, f32 panel_h)
{
    Mel_Vec4 bg = mel_vec4(0.08f, 0.08f, 0.1f, 1.0f);

    push_rect(list, x_offset, 0, panel_w, panel_h, bg);

    Mel_Vec4 title_color = mel_vec4(0.6f, 0.6f, 0.7f, 1.0f);
    mel_font_atlas_draw_text(font, font_list, S8("Trie Structure"),
        x_offset + 10.0f, 10.0f, title_color);

    if (!demo->trie.root) return;

    f32 tree_x_min = x_offset + 10.0f;
    f32 tree_x_max = x_offset + panel_w - 10.0f;
    f32 tree_y = 40.0f;

    draw_trie_node(demo->trie.root, '.', 0,
                  tree_x_min, tree_x_max, tree_y,
                  demo->input_buf, demo->input_len,
                  list, font_list, font, true);
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    s_font_handle = mel_font_atlas_load(
        .desc = mel_font_desc_load_ttf(S8("/System/Library/Fonts/Monaco.ttf")), .size = 18.0f);


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
                                      (f32)sc->extent.height, 0, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&s_graph, S8("render"),
        .fn = mel_sprite_pass_execute,
        .user = mel_sprite_pass(),
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    SDL_Log("Trie Autocomplete ready! Type to search, ESC to quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Trie Autocomplete"), .width = 1100, .height = 700);
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

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    mel_trie_free(&s_demo.trie);

    mel_vfs_unmount(S8("/"));
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);
    s_demo.cursor_blink += dt;
    if (s_demo.cursor_blink >= 1.0f)
        s_demo.cursor_blink -= 1.0f;

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_font_list);

    i32 ww, wh;
    mel_window_size_pixels(s_window_handle, &ww, &wh);
    f32 sw = (f32)ww;
    f32 sh = (f32)wh;
    f32 left_w = sw * 0.38f;
    f32 right_w = sw - left_w;

    draw_left_panel(&s_demo, &s_sprite_list, &s_font_list, s_font_handle, left_w, sh);
    draw_right_panel(&s_demo, &s_sprite_list, &s_font_list, s_font_handle, left_w, right_w, sh);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
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
