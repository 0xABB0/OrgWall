#include "font.msdf.h"
#include "font.desc.h"
#include "text.material.h"
#include "render.material_base.h"
#include "core.engine.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "collection.slotmap.h"
#include "collection.hashmap.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "math.scalar.h"
#include "texture.pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "async.job.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <msdf.h>
#include <stb_truetype.h>

#define MEL_FONT_MSDF_FIRST_CHAR 32
#define MEL_FONT_MSDF_CHAR_COUNT 96

typedef struct {
    Mel_SlotMap slotmap;
    Mel_HashMap dedup;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
} Mel__Font_MSDF_Pool;

static Mel__Font_MSDF_Pool s_pool;
static bool s_initialized;
static Mel_Gpu_Shader s_text_msdf_shader;
static Mel_Gpu_Device* s_text_msdf_dev;
static Mel_Material_Base_Id s_text_msdf_mat_id = MEL_MATERIAL_BASE_ID_INVALID;

static u64 mel__font_msdf_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_msdf_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

static void mel__font_msdf_ensure_init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    const Mel_Alloc* alloc = mel_alloc_heap();
    s_pool.alloc = alloc;
    mel_slotmap_init(&s_pool.slotmap, alloc,
        .item_size = sizeof(Mel_Font_MSDF_Entry), .initial_capacity = 8);
    mel_hashmap_init(&s_pool.dedup, mel__font_msdf_hash_key, mel__font_msdf_eq_key, alloc);
}

void mel__font_msdf_set_device(Mel_Gpu_Device* dev)
{
    mel__font_msdf_ensure_init();
    s_pool.dev = dev;
}

static void mel__font_msdf_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Font_MSDF_Entry* entries = mel_slotmap_data(&s_pool.slotmap);
    u32 count = mel_slotmap_count(&s_pool.slotmap);

    for (u32 i = 0; i < count; i++)
    {
        mel_gpu_texture_shutdown(&entries[i].texture, s_pool.dev);
        if (entries[i].desc.glyphs)
            mel_dealloc(s_pool.alloc, entries[i].desc.glyphs);
    }

    mel_slotmap_free(&s_pool.slotmap);
    mel_hashmap_free(&s_pool.dedup);
    s_pool = (Mel__Font_MSDF_Pool){0};
    s_initialized = false;
}

static void mel__font_msdf_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    if (s_text_msdf_shader._vertex != nullptr)
        mel_gpu_shader_shutdown(&s_text_msdf_shader, s_text_msdf_dev);
    mel__font_msdf_shutdown();
}

static void mel__font_msdf_on_texture_pool_ready(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    mel__font_msdf_set_device(mel_texture_pool()->dev);
}

static void mel__font_msdf_compile(void* data)
{
    (void)data;
    mel_gpu_shader_load(&s_text_msdf_shader, .path = S8("shaders/text_msdf.slang"), .dev = s_text_msdf_dev);
    mel_material_base_set_shader(s_text_msdf_mat_id, &s_text_msdf_shader);
}

static void mel__font_msdf_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_text_msdf_dev = e->dev;
    mel_job_run(e->phase_counter, mel__font_msdf_compile, NULL);
}

static void mel__font_msdf_wire(void)
{
    mel_event_channel_on(&mel_texture_pool_ready, mel__font_msdf_on_texture_pool_ready, NULL);
    mel_event_channel_on(&mel_gpu_device_ready, mel__font_msdf_on_gpu_ready, NULL);
    mel_event_channel_on(&mel_shutdown_begin, mel__font_msdf_on_shutdown, NULL);

    s_text_msdf_mat_id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("text_msdf"),
        .param_size = sizeof(Mel_Text_MSDF_Params),
        .compat = MEL_COMPAT_2D,
    });
}

__attribute__((constructor))
static void mel__font_msdf_register(void)
{
    mel__boot_register_wire(mel__font_msdf_wire);
}

static u64 mel__font_msdf_dedup_key(Mel_Font_MSDF_Load_Opt opt)
{
    u64 key = mel_slotmap_handle_pack64(opt.desc.handle);
    f32 font_size = opt.size > 0 ? opt.size : 48.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 1024;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 1024;

    key = mel_xxh64(&font_size, sizeof(font_size), key);
    key = mel_xxh64(&atlas_w, sizeof(atlas_w), key);
    key = mel_xxh64(&atlas_h, sizeof(atlas_h), key);
    return key;
}

static bool mel__font_msdf_measure_desc(Mel_Font_Descriptor* desc, str8 text, Mel_Vec2* out)
{
    if (!desc || !out) return false;
    if (str8_is_empty(text))
    {
        *out = mel_vec2(0, 0);
        return true;
    }

    f32 max_width = 0;
    f32 width = 0;
    f32 height = desc->line_height;

    for (size i = 0; i < text.len; i++)
    {
        int c = text.data[i];
        if (c == '\n')
        {
            if (width > max_width) max_width = width;
            width = 0;
            height += desc->line_height;
            continue;
        }

        if (c < (int)desc->first_codepoint ||
            c >= (int)(desc->first_codepoint + desc->glyph_count))
            continue;

        width += desc->glyphs[c - (int)desc->first_codepoint].xadvance;
    }

    if (width > max_width) max_width = width;
    *out = mel_vec2(max_width, height);
    return true;
}

Mel_Font_MSDF_Handle mel_font_msdf_load_opt(Mel_Font_MSDF_Load_Opt opt)
{
    mel__font_msdf_ensure_init();
    assert(s_pool.dev != nullptr);
    assert(mel_slotmap_handle_valid(opt.desc.handle));

    u64 hash = mel__font_msdf_dedup_key(opt);
    void* existing = mel_hashmap_get(&s_pool.dedup, (void*)(usize)hash);
    if (existing)
        return (Mel_Font_MSDF_Handle){ .handle = mel_slotmap_handle_from_ptr(existing) };

    Mel_Font_Desc* font = mel_font_desc_get(opt.desc);
    assert(font != nullptr);

    f32 font_size = opt.size > 0 ? opt.size : 48.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 1024;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 1024;
    f32 px_range = opt.px_range > 0 ? opt.px_range : 8.0f;
    u32 padding = opt.padding > 0 ? opt.padding : (u32)ceilf(px_range) + 2;
    if (padding < (u32)ceilf(px_range) + 2)
        padding = (u32)ceilf(px_range) + 2;

    u8* rgba = mel_calloc(s_pool.alloc, atlas_w * atlas_h * 4);

    f32 layout_scale = stbtt_ScaleForPixelHeight(&font->info, font_size);

    i32 max_gw = 0;
    i32 max_gh = 0;
    for (u32 c = MEL_FONT_MSDF_FIRST_CHAR; c < MEL_FONT_MSDF_FIRST_CHAR + MEL_FONT_MSDF_CHAR_COUNT; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&font->info, (int)c);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&font->info, glyph_idx, layout_scale, layout_scale, &x0, &y0, &x1, &y1);
        max_gw = mel_maxi(max_gw, x1 - x0);
        max_gh = mel_maxi(max_gh, y1 - y0);
    }

    u32 cell_w = (u32)mel_maxi(max_gw, 1) + padding * 2;
    u32 cell_h = (u32)mel_maxi(max_gh, 1) + padding * 2;

    f32 gen_scale = stbtt_ScaleForMappingEmToPixels(&font->info, (f32)cell_h);

    Mel_Font_MSDF_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;
    entry.px_range = px_range;
    entry.font_desc = opt.desc;
    entry.desc.first_codepoint = MEL_FONT_MSDF_FIRST_CHAR;
    entry.desc.glyph_count = MEL_FONT_MSDF_CHAR_COUNT;
    entry.desc.glyphs = mel_alloc_array(s_pool.alloc, Mel_Font_Glyph, entry.desc.glyph_count);
    entry.desc.ascent = (f32)font->ascent * gen_scale;
    entry.desc.line_height = (f32)(font->ascent - font->descent + font->line_gap) * gen_scale;
    entry.desc.bake_size = font_size;
    u32 atlas_gutter = (u32)mel_maxi((i32)padding, 8);

    float* glyph_bitmap = mel_alloc_array(s_pool.alloc, float, (usize)cell_w * (usize)cell_h * 3);

    u32 x = atlas_gutter;
    u32 y = atlas_gutter;
    u32 row_height = cell_h;

    for (u32 c = MEL_FONT_MSDF_FIRST_CHAR; c < MEL_FONT_MSDF_FIRST_CHAR + MEL_FONT_MSDF_CHAR_COUNT; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&font->info, (int)c);
        int advance = 0;
        int lsb = 0;
        stbtt_GetGlyphHMetrics(&font->info, glyph_idx, &advance, &lsb);

        u32 idx = c - MEL_FONT_MSDF_FIRST_CHAR;
        entry.desc.glyphs[idx].xadvance = (f32)advance * gen_scale;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&font->info, glyph_idx, gen_scale, gen_scale, &x0, &y0, &x1, &y1);
        i32 gw = x1 - x0;
        i32 gh = y1 - y0;

        if (gw <= 0 || gh <= 0)
            continue;

        if (x + cell_w + atlas_gutter > atlas_w)
        {
            x = atlas_gutter;
            y += row_height + padding;
            row_height = cell_h;
        }

        if (y + cell_h + atlas_gutter > atlas_h)
        {
            mel_dealloc(s_pool.alloc, glyph_bitmap);
            mel_dealloc(s_pool.alloc, entry.desc.glyphs);
            mel_dealloc(s_pool.alloc, rgba);
            return MEL_FONT_MSDF_HANDLE_NULL;
        }

        memset(glyph_bitmap, 0, sizeof(float) * (usize)cell_w * (usize)cell_h * 3);
        ex_metrics_t metrics = {0};
        if (!ex_msdf_glyph_mem(&font->info, c, cell_w, cell_h, glyph_bitmap, &metrics, 0))
            continue;

        for (u32 row = 0; row < cell_h; row++)
        {
            for (u32 col = 0; col < cell_w; col++)
            {
                usize src = (((usize)row * cell_w) + col) * 3;
                usize dst = (((usize)(y + row) * atlas_w) + (x + col)) * 4;
                f32 r = glyph_bitmap[src + 0];
                f32 g = glyph_bitmap[src + 1];
                f32 b = glyph_bitmap[src + 2];
                rgba[dst + 0] = (u8)(mel_clampf(r, 0.0f, 1.0f) * 255.0f);
                rgba[dst + 1] = (u8)(mel_clampf(g, 0.0f, 1.0f) * 255.0f);
                rgba[dst + 2] = (u8)(mel_clampf(b, 0.0f, 1.0f) * 255.0f);
                rgba[dst + 3] = 255;
            }
        }

        f32 half_pad_x = ((f32)cell_w - (f32)gw) * 0.5f;
        f32 half_pad_y = ((f32)cell_h - (f32)gh) * 0.5f;

        entry.desc.glyphs[idx] = (Mel_Font_Glyph){
            .x0 = (f32)x0 - half_pad_x,
            .y0 = (f32)y0 - half_pad_y,
            .x1 = (f32)x1 + half_pad_x,
            .y1 = (f32)y1 + half_pad_y,
            .u0 = (f32)x / (f32)atlas_w,
            .v0 = (f32)y / (f32)atlas_h,
            .u1 = (f32)(x + cell_w) / (f32)atlas_w,
            .v1 = (f32)(y + cell_h) / (f32)atlas_h,
            .xadvance = entry.desc.glyphs[idx].xadvance,
        };

        x += cell_w + atlas_gutter;
    }

    mel_dealloc(s_pool.alloc, glyph_bitmap);

    mel_gpu_texture_init(&entry.texture, s_pool.dev,
        .pixels = rgba, .width = atlas_w, .height = atlas_h,
        .format = MEL_GPU_FORMAT_R8G8B8A8_UNORM);
    mel_dealloc(s_pool.alloc, rgba);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&s_pool.slotmap, &entry);
    mel_hashmap_put(&s_pool.dedup, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));

    SDL_Log("font.msdf: loaded %.0fpx, atlas %ux%u", font_size, atlas_w, atlas_h);
    return (Mel_Font_MSDF_Handle){ .handle = sm_handle };
}

Mel_Font_MSDF_Entry* mel_font_msdf_get(Mel_Font_MSDF_Handle handle)
{
    mel__font_msdf_ensure_init();
    return mel_slotmap_get(&s_pool.slotmap, handle.handle);
}

Mel_Gpu_Texture* mel_font_msdf_get_texture(Mel_Font_MSDF_Handle handle)
{
    Mel_Font_MSDF_Entry* entry = mel_font_msdf_get(handle);
    return entry ? &entry->texture : nullptr;
}

Mel_Vec2 mel_font_msdf_measure_text(Mel_Font_MSDF_Handle handle, str8 text)
{
    Mel_Font_MSDF_Entry* entry = mel_font_msdf_get(handle);
    assert(entry != nullptr);
    Mel_Vec2 out = {0};
    mel__font_msdf_measure_desc(&entry->desc, text, &out);
    return out;
}
