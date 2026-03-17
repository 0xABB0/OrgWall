#include "font.atlas.h"
#include "font.desc.h"
#include "texture.pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "collection.hashmap.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "gpu.device.fwd.h"

#include <SDL3/SDL.h>
#include <stb_truetype.h>
#include <string.h>

typedef struct {
    Mel_SlotMap slotmap;
    Mel_HashMap dedup;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    Mel_Texture_Pool* texture_pool;
} Mel__Font_Atlas_Pool;

static Mel__Font_Atlas_Pool s_pool;
static bool s_initialized;

Mel_Event_Channel mel_font_pool_ready;

static u64 mel__font_atlas_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_atlas_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

static void mel__font_atlas_ensure_init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    const Mel_Alloc* alloc = mel_alloc_heap();
    s_pool.alloc = alloc;
    mel_slotmap_init(&s_pool.slotmap, alloc,
        .item_size = sizeof(Mel_Font_Atlas_Entry), .initial_capacity = 8);
    mel_hashmap_init(&s_pool.dedup, mel__font_atlas_hash_key, mel__font_atlas_eq_key, alloc);
}

void mel__font_atlas_set_device(Mel_Gpu_Device* dev, Mel_Texture_Pool* texture_pool)
{
    mel__font_atlas_ensure_init();
    s_pool.dev = dev;
    s_pool.texture_pool = texture_pool;
}

void mel__font_atlas_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Font_Atlas_Entry* entries = mel_slotmap_data(&s_pool.slotmap);
    u32 count = mel_slotmap_count(&s_pool.slotmap);

    for (u32 i = 0; i < count; i++)
    {
        mel_gpu_texture_shutdown(&entries[i].atlas_texture, s_pool.dev);
        if (entries[i].desc.glyphs)
            mel_dealloc(s_pool.alloc, entries[i].desc.glyphs);
    }

    mel_slotmap_free(&s_pool.slotmap);
    mel_hashmap_free(&s_pool.dedup);
    s_pool = (Mel__Font_Atlas_Pool){0};
    s_initialized = false;
}

static void mel__font_pool_on_texture_pool_ready(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    mel__font_atlas_set_device(mel_sprite_pass()->dev, mel_texture_pool());
    mel_event_channel_fire(&mel_font_pool_ready, NULL);
}

static void mel__font_pool_wire(void)
{
    mel_event_channel_on(&mel_texture_pool_ready, mel__font_pool_on_texture_pool_ready, NULL);
}

__attribute__((constructor))
static void mel__font_pool_register(void)
{
    mel_event_channel_init(&mel_font_pool_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__font_pool_wire);
}

__attribute__((destructor))
static void mel__font_pool_unregister(void)
{
    mel_event_channel_destroy(&mel_font_pool_ready);
}

#define MEL_FONT_ATLAS_FIRST_CHAR 32
#define MEL_FONT_ATLAS_CHAR_COUNT 96

static u64 mel__font_atlas_dedup_key(Mel_Font_Atlas_Load_Opt opt)
{
    u64 key = mel_slotmap_handle_pack64(opt.desc.handle);
    f32 font_size = opt.size > 0 ? opt.size : 24.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 512;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 512;

    key = mel_xxh64(&font_size, sizeof(font_size), key);
    key = mel_xxh64(&atlas_w, sizeof(atlas_w), key);
    key = mel_xxh64(&atlas_h, sizeof(atlas_h), key);
    return key;
}

Mel_Font_Atlas_Handle mel_font_atlas_load_opt(Mel_Font_Atlas_Load_Opt opt)
{
    mel__font_atlas_ensure_init();
    assert(s_pool.dev != nullptr);
    assert(mel_slotmap_handle_valid(opt.desc.handle));

    u64 hash = mel__font_atlas_dedup_key(opt);

    void* existing = mel_hashmap_get(&s_pool.dedup, (void*)(usize)hash);
    if (existing)
    {
        Mel_Font_Atlas_Handle h = { .handle = mel_slotmap_handle_from_ptr(existing) };
        return h;
    }

    Mel_Font_Desc* font = mel_font_desc_get(opt.desc);
    assert(font != nullptr);

    f32 font_size = opt.size > 0 ? opt.size : 24.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 512;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 512;

    u8* atlas_bitmap = mel_alloc(s_pool.alloc, (usize)(atlas_w * atlas_h));
    stbtt_bakedchar cdata[MEL_FONT_ATLAS_CHAR_COUNT];
    stbtt_BakeFontBitmap(font->data, 0, font_size, atlas_bitmap, (int)atlas_w, (int)atlas_h,
                         MEL_FONT_ATLAS_FIRST_CHAR, MEL_FONT_ATLAS_CHAR_COUNT, cdata);

    u8* rgba = mel_alloc(s_pool.alloc, (usize)(atlas_w * atlas_h * 4));
    for (u32 i = 0; i < atlas_w * atlas_h; i++)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas_bitmap[i];
    }
    mel_dealloc(s_pool.alloc, atlas_bitmap);

    Mel_Font_Atlas_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;
    entry.font_desc = opt.desc;

    mel_gpu_texture_init(&entry.atlas_texture, s_pool.dev,
        .pixels = rgba, .width = atlas_w, .height = atlas_h);
    mel_dealloc(s_pool.alloc, rgba);

    f32 scale = stbtt_ScaleForPixelHeight(&font->info, font_size);

    entry.desc.first_codepoint = MEL_FONT_ATLAS_FIRST_CHAR;
    entry.desc.glyph_count = MEL_FONT_ATLAS_CHAR_COUNT;
    entry.desc.ascent = (f32)font->ascent * scale;
    entry.desc.line_height = (f32)(font->ascent - font->descent + font->line_gap) * scale;

    entry.desc.glyphs = mel_alloc_array(s_pool.alloc, Mel_Font_Glyph, MEL_FONT_ATLAS_CHAR_COUNT);
    f32 inv_w = 1.0f / (f32)atlas_w;
    f32 inv_h = 1.0f / (f32)atlas_h;

    for (i32 i = 0; i < MEL_FONT_ATLAS_CHAR_COUNT; i++)
    {
        stbtt_bakedchar* bc = &cdata[i];
        Mel_Font_Glyph* g = &entry.desc.glyphs[i];
        g->x0 = bc->xoff;
        g->y0 = bc->yoff;
        g->x1 = bc->xoff + (f32)(bc->x1 - bc->x0);
        g->y1 = bc->yoff + (f32)(bc->y1 - bc->y0);
        g->u0 = (f32)bc->x0 * inv_w;
        g->v0 = (f32)bc->y0 * inv_h;
        g->u1 = (f32)bc->x1 * inv_w;
        g->v1 = (f32)bc->y1 * inv_h;
        g->xadvance = bc->xadvance;
    }

    if (s_pool.texture_pool)
        entry.tex_handle = mel_texture_pool_register(s_pool.texture_pool, &entry.atlas_texture);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&s_pool.slotmap, &entry);
    mel_hashmap_put(&s_pool.dedup, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));
    return (Mel_Font_Atlas_Handle){ .handle = sm_handle };
}

Mel_Font_Atlas_Handle mel__font_atlas_insert_entry(const Mel_Font_Atlas_Entry* entry)
{
    mel__font_atlas_ensure_init();
    assert(entry != nullptr);
    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&s_pool.slotmap, entry);
    return (Mel_Font_Atlas_Handle){ .handle = sm_handle };
}

Mel_Font_Atlas_Entry* mel_font_atlas_get(Mel_Font_Atlas_Handle handle)
{
    mel__font_atlas_ensure_init();
    return mel_slotmap_get(&s_pool.slotmap, handle.handle);
}

Mel_Gpu_Texture* mel_font_atlas_get_texture(Mel_Font_Atlas_Handle handle)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_get(handle);
    return entry ? &entry->atlas_texture : nullptr;
}

Mel_Texture_Handle mel_font_atlas_tex_handle(Mel_Font_Atlas_Handle handle)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_get(handle);
    assert(entry != nullptr);
    return entry->tex_handle;
}

void mel_font_atlas_draw_text_ex(Mel_Font_Atlas_Handle handle,
    Mel_Render_List* list, str8 text, f32 x, f32 y, Mel_Vec4 color, u64 sort_key)
{
    assert(list != nullptr);

    Mel_Font_Atlas_Entry* entry = mel_font_atlas_get(handle);
    assert(entry != nullptr);

    if (str8_is_empty(text)) return;

    f32 cursor_x = x;
    f32 cursor_y = y + entry->desc.ascent;

    for (size i = 0; i < text.len; i++)
    {
        int c = text.data[i];

        if (c == '\n')
        {
            cursor_x = x;
            cursor_y += entry->desc.line_height;
            continue;
        }

        if (c < (int)entry->desc.first_codepoint ||
            c >= (int)(entry->desc.first_codepoint + entry->desc.glyph_count))
            continue;

        int idx = c - (int)entry->desc.first_codepoint;
        Mel_Font_Glyph* g = &entry->desc.glyphs[idx];

        f32 gx = cursor_x + g->x0;
        f32 gy = cursor_y + g->y0;
        f32 gw = g->x1 - g->x0;
        f32 gh = g->y1 - g->y0;

        if (gw > 0 && gh > 0)
        {
            Mel_Sprite_Entry* e = mel_render_list_push(list, sort_key);
            *e = (Mel_Sprite_Entry){
                .pos = mel_vec2(gx, gy),
                .size = mel_vec2(gw, gh),
                .uv = mel_rect(g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0),
                .color = color,
                .tex = entry->tex_handle,
            };
        }

        cursor_x += g->xadvance;
    }
}

Mel_Vec2 mel_font_atlas_measure_text(Mel_Font_Atlas_Handle handle, str8 text)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_get(handle);
    assert(entry != nullptr);

    if (str8_is_empty(text)) return mel_vec2(0, 0);

    f32 max_width = 0;
    f32 width = 0;
    f32 height = entry->desc.line_height;

    for (size i = 0; i < text.len; i++)
    {
        int c = text.data[i];

        if (c == '\n')
        {
            if (width > max_width) max_width = width;
            width = 0;
            height += entry->desc.line_height;
            continue;
        }

        if (c < (int)entry->desc.first_codepoint ||
            c >= (int)(entry->desc.first_codepoint + entry->desc.glyph_count))
            continue;

        int idx = c - (int)entry->desc.first_codepoint;
        width += entry->desc.glyphs[idx].xadvance;
    }

    if (width > max_width) max_width = width;

    return mel_vec2(max_width, height);
}
