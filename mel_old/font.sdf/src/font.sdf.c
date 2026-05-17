#include <font.sdf/font.sdf.h>
#include <core/compiler.h>
#include <font/font.desc.h>
#include "text.material.h"
#include "render.material_base.h"
#include "core.engine.h"
#include <string/str8.h>
#include <hash/xxh.h>
#include <collection.slotmap/slotmap.h>
#include <collection.map.hashmap/hashmap.h>
#include <allocator/allocator.h>
#include <allocator.heap/allocator.heap.h>
#include "gpu.device.h"
#include "gpu.shader.h"
#include <math/scalar.h>
#include "texture.pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include <async.job/job.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <stb_truetype.h>

#define MEL_FONT_SDF_FIRST_CHAR 32
#define MEL_FONT_SDF_CHAR_COUNT 96

typedef struct {
    Mel_SlotMap slotmap;
    Mel_HashMap dedup;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
} Mel__Font_SDF_Pool;

static Mel__Font_SDF_Pool s_pool;
static bool s_initialized;
static Mel_Gpu_Shader s_text_sdf_shader;
static Mel_Gpu_Device* s_text_sdf_dev;
static Mel_Material_Base_Id s_text_sdf_mat_id = MEL_MATERIAL_BASE_ID_INVALID;

static u64 mel__font_sdf_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_sdf_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

static void mel__font_sdf_ensure_init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    const Mel_Alloc* alloc = mel_alloc_heap();
    s_pool.alloc = alloc;
    mel_slotmap_init(&s_pool.slotmap, alloc,
        .item_size = sizeof(Mel_Font_SDF_Entry), .initial_capacity = 8);
    mel_hashmap_init(&s_pool.dedup, mel__font_sdf_hash_key, mel__font_sdf_eq_key, alloc);
}

void mel__font_sdf_set_device(Mel_Gpu_Device* dev)
{
    mel__font_sdf_ensure_init();
    s_pool.dev = dev;
}

static void mel__font_sdf_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Font_SDF_Entry* entries = mel_slotmap_data(&s_pool.slotmap);
    u32 count = mel_slotmap_count(&s_pool.slotmap);

    for (u32 i = 0; i < count; i++)
    {
        mel_gpu_texture_shutdown(&entries[i].texture, s_pool.dev);
        if (entries[i].desc.glyphs)
            mel_dealloc(s_pool.alloc, entries[i].desc.glyphs);
    }

    mel_slotmap_free(&s_pool.slotmap);
    mel_hashmap_free(&s_pool.dedup);
    s_pool = (Mel__Font_SDF_Pool){0};
    s_initialized = false;
}

static void mel__font_sdf_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    if (s_text_sdf_shader._vertex != nullptr)
        mel_gpu_shader_shutdown(&s_text_sdf_shader, s_text_sdf_dev);
    mel__font_sdf_shutdown();
}

static void mel__font_sdf_on_texture_pool_ready(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    mel__font_sdf_set_device(mel_texture_pool()->dev);
}

static void mel__font_sdf_compile(void* data)
{
    (void)data;
    mel_gpu_shader_load(&s_text_sdf_shader, .path = S8("shaders/text_sdf.slang"), .dev = s_text_sdf_dev);
    mel_material_base_set_shader(s_text_sdf_mat_id, &s_text_sdf_shader);
}

static void mel__font_sdf_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_text_sdf_dev = e->dev;
    mel_job_run(e->phase_counter, mel__font_sdf_compile, NULL);
}

static void mel__font_sdf_wire(void)
{
    mel_event_channel_on(&mel_texture_pool_ready, mel__font_sdf_on_texture_pool_ready, NULL);
    mel_event_channel_on(&mel_gpu_device_ready, mel__font_sdf_on_gpu_ready, NULL);
    mel_event_channel_on(&mel_shutdown_begin, mel__font_sdf_on_shutdown, NULL);

    s_text_sdf_mat_id = mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("text_sdf"),
        .param_size = sizeof(Mel_Text_SDF_Params),
        .compat = MEL_COMPAT_2D,
    });
}

MEL_CONSTRUCTOR
static void mel__font_sdf_register(void)
{
    mel__boot_register_wire(mel__font_sdf_wire);
}

static u64 mel__font_sdf_dedup_key(Mel_Font_SDF_Load_Opt opt)
{
    u64 key = mel_slotmap_handle_pack64(opt.desc.handle);
    f32 font_size = opt.size > 0 ? opt.size : 32.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 1024;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 1024;

    key = mel_xxh64(&font_size, sizeof(font_size), key);
    key = mel_xxh64(&atlas_w, sizeof(atlas_w), key);
    key = mel_xxh64(&atlas_h, sizeof(atlas_h), key);
    return key;
}

static bool mel__font_sdf_measure_desc(Mel_Font_Descriptor* desc, str8 text, Mel_Vec2* out)
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

Mel_Font_SDF_Handle mel_font_sdf_load_opt(Mel_Font_SDF_Load_Opt opt)
{
    mel__font_sdf_ensure_init();
    assert(s_pool.dev != nullptr);
    assert(mel_slotmap_handle_valid(opt.desc.handle));

    u64 hash = mel__font_sdf_dedup_key(opt);
    void* existing = mel_hashmap_get(&s_pool.dedup, (void*)(usize)hash);
    if (existing)
        return (Mel_Font_SDF_Handle){ .handle = mel_slotmap_handle_from_ptr(existing) };

    Mel_Font_Desc* font = mel_font_desc_get(opt.desc);
    assert(font != nullptr);

    f32 font_size = opt.size > 0 ? opt.size : 32.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 1024;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 1024;
    u32 padding = opt.padding > 0 ? opt.padding : 4;
    f32 px_range = opt.px_range > 0 ? opt.px_range : 4.0f;
    u32 on_edge = 128;
    f32 pixel_dist_scale = (f32)on_edge / px_range;

    f32 scale = stbtt_ScaleForPixelHeight(&font->info, font_size);

    u8* atlas_bitmap = mel_alloc(s_pool.alloc, (usize)(atlas_w * atlas_h));
    memset(atlas_bitmap, 0, atlas_w * atlas_h);

    u32 pen_x = padding, pen_y = padding;
    u32 row_height = 0;

    Mel_Font_SDF_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;
    entry.px_range = px_range;
    entry.font_desc = opt.desc;
    entry.desc.first_codepoint = MEL_FONT_SDF_FIRST_CHAR;
    entry.desc.glyph_count = MEL_FONT_SDF_CHAR_COUNT;
    entry.desc.ascent = (f32)font->ascent * scale;
    entry.desc.line_height = (f32)(font->ascent - font->descent + font->line_gap) * scale;
    entry.desc.bake_size = font_size;
    entry.desc.glyphs = mel_alloc_array(s_pool.alloc, Mel_Font_Glyph, MEL_FONT_SDF_CHAR_COUNT);

    f32 inv_w = 1.0f / (f32)atlas_w;
    f32 inv_h = 1.0f / (f32)atlas_h;

    for (i32 i = 0; i < MEL_FONT_SDF_CHAR_COUNT; i++)
    {
        int codepoint = MEL_FONT_SDF_FIRST_CHAR + i;
        int gw, gh, xoff, yoff;
        u8* sdf = stbtt_GetCodepointSDF(&font->info, scale, codepoint, (int)padding,
                                         (u8)on_edge, pixel_dist_scale, &gw, &gh, &xoff, &yoff);

        Mel_Font_Glyph* g = &entry.desc.glyphs[i];
        *g = (Mel_Font_Glyph){0};

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        g->xadvance = (f32)advance * scale;

        if (sdf && gw > 0 && gh > 0)
        {
            if (pen_x + (u32)gw + padding > atlas_w)
            {
                pen_x = padding;
                pen_y += row_height + padding;
                row_height = 0;
            }

            for (i32 row = 0; row < gh; row++)
                memcpy(&atlas_bitmap[(pen_y + (u32)row) * atlas_w + pen_x], &sdf[row * gw], (size_t)gw);

            g->x0 = (f32)xoff;
            g->y0 = (f32)yoff;
            g->x1 = (f32)(xoff + gw);
            g->y1 = (f32)(yoff + gh);
            g->u0 = (f32)pen_x * inv_w;
            g->v0 = (f32)pen_y * inv_h;
            g->u1 = (f32)(pen_x + (u32)gw) * inv_w;
            g->v1 = (f32)(pen_y + (u32)gh) * inv_h;

            pen_x += (u32)gw + padding;
            if ((u32)gh > row_height) row_height = (u32)gh;
        }

        if (sdf) stbtt_FreeSDF(sdf, NULL);
    }

    u8* rgba = mel_alloc(s_pool.alloc, (usize)(atlas_w * atlas_h * 4));
    for (u32 j = 0; j < atlas_w * atlas_h; j++)
    {
        rgba[j * 4 + 0] = atlas_bitmap[j];
        rgba[j * 4 + 1] = atlas_bitmap[j];
        rgba[j * 4 + 2] = atlas_bitmap[j];
        rgba[j * 4 + 3] = 255;
    }
    mel_dealloc(s_pool.alloc, atlas_bitmap);

    mel_gpu_texture_init(&entry.texture, s_pool.dev,
        .pixels = rgba, .width = atlas_w, .height = atlas_h,
        .format = MEL_GPU_FORMAT_R8G8B8A8_UNORM);
    mel_dealloc(s_pool.alloc, rgba);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&s_pool.slotmap, &entry);
    mel_hashmap_put(&s_pool.dedup, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));
    return (Mel_Font_SDF_Handle){ .handle = sm_handle };
}

Mel_Font_SDF_Entry* mel_font_sdf_get(Mel_Font_SDF_Handle handle)
{
    mel__font_sdf_ensure_init();
    return mel_slotmap_get(&s_pool.slotmap, handle.handle);
}

Mel_Gpu_Texture* mel_font_sdf_get_texture(Mel_Font_SDF_Handle handle)
{
    Mel_Font_SDF_Entry* entry = mel_font_sdf_get(handle);
    return entry ? &entry->texture : nullptr;
}

Mel_Vec2 mel_font_sdf_measure_text(Mel_Font_SDF_Handle handle, str8 text)
{
    Mel_Font_SDF_Entry* entry = mel_font_sdf_get(handle);
    assert(entry != nullptr);
    Mel_Vec2 out = {0};
    mel__font_sdf_measure_desc(&entry->desc, text, &out);
    return out;
}
