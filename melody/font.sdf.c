#include "font.sdf.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include "allocator.h"
#include "math.scalar.h"
#include "vfs.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stb_truetype.h>

#define MEL_FONT_SDF_FIRST_CHAR 32
#define MEL_FONT_SDF_CHAR_COUNT 96

static u64 mel__font_sdf_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_sdf_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

typedef struct {
    Mel_Gpu_Image* image;
    Mel_Gpu_Buffer* staging;
    u32 width;
    u32 height;
} Mel__Font_SDF_Upload;

static void mel__font_sdf_upload_cmd(VkCommandBuffer cmd, void* user)
{
    Mel__Font_SDF_Upload* data = user;

    mel_gpu_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { data->width, data->height, 1 },
    };

    vkCmdCopyBufferToImage(cmd, data->staging->buffer, data->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    mel_gpu_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

void mel_font_sdf_pool_init(Mel_Font_SDF_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);

    *pool = (Mel_Font_SDF_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Font_SDF_Entry), .initial_capacity = 8);
    mel_hashmap_init(&pool->path_to_handle, mel__font_sdf_pool_hash_key, mel__font_sdf_pool_eq_key, alloc);
}

void mel_font_sdf_pool_shutdown(Mel_Font_SDF_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Font_SDF_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);
    for (u32 i = 0; i < count; i++)
    {
        mel_gpu_texture_shutdown(&entries[i].texture, pool->dev);
        if (entries[i].desc.glyphs)
            mel_dealloc(pool->alloc, entries[i].desc.glyphs);
    }

    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);
    *pool = (Mel_Font_SDF_Pool){0};
}

Mel_Font_Handle mel_font_sdf_pool_load_opt(Mel_Font_SDF_Pool* pool, Mel_Font_SDF_Load_Opt opt)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(opt.path));

    u64 hash = str8_hash(opt.path);
    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
        return (Mel_Font_Handle){ .handle = mel_slotmap_handle_from_ptr(existing) };

    i64 fsize = 0;
    u8* ttf_data = mel_vfs_read_file(opt.path, &fsize, pool->alloc);
    if (!ttf_data)
    {
        SDL_Log("font.sdf: failed to read '%.*s'", (int)opt.path.len, opt.path.data);
        return MEL_FONT_HANDLE_NULL;
    }

    f32 font_size = opt.size > 0 ? opt.size : 32.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 1024;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 1024;
    u32 padding = opt.padding > 0 ? opt.padding : 4;
    f32 px_range = opt.px_range > 0 ? opt.px_range : 4.0f;
    u32 on_edge = 128;
    f32 pixel_dist_scale = (f32)on_edge / px_range;

    stbtt_fontinfo font_info;
    stbtt_InitFont(&font_info, ttf_data, 0);
    f32 scale = stbtt_ScaleForPixelHeight(&font_info, font_size);

    int ascent_i, descent_i, line_gap_i;
    stbtt_GetFontVMetrics(&font_info, &ascent_i, &descent_i, &line_gap_i);

    u8* atlas_bitmap = mel_alloc(pool->alloc, (usize)(atlas_w * atlas_h));
    memset(atlas_bitmap, 0, atlas_w * atlas_h);

    u32 pen_x = padding, pen_y = padding;
    u32 row_height = 0;

    Mel_Font_SDF_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;
    entry.px_range = px_range;
    entry.desc.first_codepoint = MEL_FONT_SDF_FIRST_CHAR;
    entry.desc.glyph_count = MEL_FONT_SDF_CHAR_COUNT;
    entry.desc.ascent = (f32)ascent_i * scale;
    entry.desc.line_height = (f32)(ascent_i - descent_i + line_gap_i) * scale;
    entry.desc.glyphs = mel_alloc_array(pool->alloc, Mel_Font_Glyph, MEL_FONT_SDF_CHAR_COUNT);

    f32 inv_w = 1.0f / (f32)atlas_w;
    f32 inv_h = 1.0f / (f32)atlas_h;

    for (i32 i = 0; i < MEL_FONT_SDF_CHAR_COUNT; i++)
    {
        int codepoint = MEL_FONT_SDF_FIRST_CHAR + i;
        int gw, gh, xoff, yoff;
        u8* sdf = stbtt_GetCodepointSDF(&font_info, scale, codepoint, (int)padding,
                                         (u8)on_edge, pixel_dist_scale, &gw, &gh, &xoff, &yoff);

        Mel_Font_Glyph* g = &entry.desc.glyphs[i];
        *g = (Mel_Font_Glyph){0};

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &lsb);
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

    u8* rgba = mel_alloc(pool->alloc, (usize)(atlas_w * atlas_h * 4));
    for (u32 j = 0; j < atlas_w * atlas_h; j++)
    {
        rgba[j * 4 + 0] = 255;
        rgba[j * 4 + 1] = 255;
        rgba[j * 4 + 2] = 255;
        rgba[j * 4 + 3] = atlas_bitmap[j];
    }
    mel_dealloc(pool->alloc, atlas_bitmap);

    mel_gpu_texture_init(&entry.texture, pool->dev,
        .pixels = rgba, .width = atlas_w, .height = atlas_h);
    mel_dealloc(pool->alloc, rgba);
    mel_dealloc(pool->alloc, ttf_data);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));
    return (Mel_Font_Handle){ .handle = sm_handle };
}

Mel_Font_SDF_Entry* mel_font_sdf_pool_get(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle)
{
    assert(pool != nullptr);
    return mel_slotmap_get(&pool->slotmap, handle.handle);
}

Mel_Gpu_Texture* mel_font_sdf_pool_get_texture(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle)
{
    Mel_Font_SDF_Entry* entry = mel_font_sdf_pool_get(pool, handle);
    return entry ? &entry->texture : nullptr;
}

Mel_Vec2 mel_font_sdf_measure_text(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle, str8 text)
{
    Mel_Font_SDF_Entry* entry = mel_font_sdf_pool_get(pool, handle);
    assert(entry != nullptr);
    Mel_Vec2 out = {0};
    mel__font_sdf_measure_desc(&entry->desc, text, &out);
    return out;
}
