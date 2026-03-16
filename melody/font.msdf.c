#include "font.msdf.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include "allocator.h"
#include "math.scalar.h"
#include "vfs.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <msdf.h>
#include <stb_truetype.h>

#define MEL_FONT_MSDF_FIRST_CHAR 32
#define MEL_FONT_MSDF_CHAR_COUNT 96

static u64 mel__font_msdf_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_msdf_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

typedef struct {
    Mel_Gpu_Image* image;
    Mel_Gpu_Buffer* staging;
    u32 width;
    u32 height;
} Mel__Font_MSDF_Upload;

static void mel__font_msdf_upload_cmd(VkCommandBuffer cmd, void* user)
{
    Mel__Font_MSDF_Upload* data = user;

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

void mel_font_msdf_pool_init(Mel_Font_MSDF_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);

    *pool = (Mel_Font_MSDF_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Font_MSDF_Entry), .initial_capacity = 8);
    mel_hashmap_init(&pool->path_to_handle, mel__font_msdf_pool_hash_key, mel__font_msdf_pool_eq_key, alloc);
}

void mel_font_msdf_pool_shutdown(Mel_Font_MSDF_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Font_MSDF_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);
    for (u32 i = 0; i < count; i++)
    {
        mel_gpu_texture_shutdown(&entries[i].texture, pool->dev);
        if (entries[i].desc.glyphs)
            mel_dealloc(pool->alloc, entries[i].desc.glyphs);
    }

    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);
    *pool = (Mel_Font_MSDF_Pool){0};
}

Mel_Font_Handle mel_font_msdf_pool_load_opt(Mel_Font_MSDF_Pool* pool, Mel_Font_MSDF_Load_Opt opt)
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
        SDL_Log("font.msdf: failed to open '%.*s'", (int)opt.path.len, opt.path.data);
        return MEL_FONT_HANDLE_NULL;
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_data, 0))
    {
        mel_dealloc(pool->alloc, ttf_data);
        return MEL_FONT_HANDLE_NULL;
    }

    f32 font_size = opt.size > 0 ? opt.size : 48.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 1024;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 1024;
    f32 px_range = opt.px_range > 0 ? opt.px_range : 8.0f;
    u32 padding = opt.padding > 0 ? opt.padding : (u32)ceilf(px_range) + 2;
    if (padding < (u32)ceilf(px_range) + 2)
        padding = (u32)ceilf(px_range) + 2;

    u8* rgba = mel_calloc(pool->alloc, atlas_w * atlas_h * 4);

    int ascent_i, descent_i, line_gap_i;
    stbtt_GetFontVMetrics(&info, &ascent_i, &descent_i, &line_gap_i);

    f32 layout_scale = stbtt_ScaleForPixelHeight(&info, font_size);

    i32 max_gw = 0;
    i32 max_gh = 0;
    for (u32 c = MEL_FONT_MSDF_FIRST_CHAR; c < MEL_FONT_MSDF_FIRST_CHAR + MEL_FONT_MSDF_CHAR_COUNT; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&info, (int)c);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&info, glyph_idx, layout_scale, layout_scale, &x0, &y0, &x1, &y1);
        max_gw = mel_maxi(max_gw, x1 - x0);
        max_gh = mel_maxi(max_gh, y1 - y0);
    }

    u32 cell_w = (u32)mel_maxi(max_gw, 1) + padding * 2;
    u32 cell_h = (u32)mel_maxi(max_gh, 1) + padding * 2;

    f32 gen_scale = stbtt_ScaleForMappingEmToPixels(&info, (f32)cell_h);

    Mel_Font_MSDF_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;
    entry.px_range = px_range;
    entry.desc.first_codepoint = MEL_FONT_MSDF_FIRST_CHAR;
    entry.desc.glyph_count = MEL_FONT_MSDF_CHAR_COUNT;
    entry.desc.glyphs = mel_alloc_array(pool->alloc, Mel_Font_Glyph, entry.desc.glyph_count);
    entry.desc.ascent = (f32)ascent_i * gen_scale;
    entry.desc.line_height = (f32)(ascent_i - descent_i + line_gap_i) * gen_scale;
    u32 atlas_gutter = (u32)mel_maxi((i32)padding, 8);

    float* glyph_bitmap = mel_alloc_array(pool->alloc, float, (usize)cell_w * (usize)cell_h * 3);

    u32 x = atlas_gutter;
    u32 y = atlas_gutter;
    u32 row_height = cell_h;

    for (u32 c = MEL_FONT_MSDF_FIRST_CHAR; c < MEL_FONT_MSDF_FIRST_CHAR + MEL_FONT_MSDF_CHAR_COUNT; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&info, (int)c);
        int advance = 0;
        int lsb = 0;
        stbtt_GetGlyphHMetrics(&info, glyph_idx, &advance, &lsb);

        u32 idx = c - MEL_FONT_MSDF_FIRST_CHAR;
        entry.desc.glyphs[idx].xadvance = (f32)advance * gen_scale;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&info, glyph_idx, gen_scale, gen_scale, &x0, &y0, &x1, &y1);
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
            mel_dealloc(pool->alloc, glyph_bitmap);
            mel_dealloc(pool->alloc, entry.desc.glyphs);
            mel_dealloc(pool->alloc, rgba);
            mel_dealloc(pool->alloc, ttf_data);
            return MEL_FONT_HANDLE_NULL;
        }

        memset(glyph_bitmap, 0, sizeof(float) * (usize)cell_w * (usize)cell_h * 3);
        ex_metrics_t metrics = {0};
        if (!ex_msdf_glyph_mem(&info, c, cell_w, cell_h, glyph_bitmap, &metrics, 0))
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

    mel_dealloc(pool->alloc, glyph_bitmap);
    mel_dealloc(pool->alloc, ttf_data);

    Mel_Gpu_Buffer staging;
    u32 image_size = atlas_w * atlas_h * 4;
    mel_gpu_buffer_init(&staging, pool->dev,
        .size = image_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_ONLY);
    mel_gpu_buffer_upload(&staging, pool->dev, rgba, image_size, 0);
    mel_dealloc(pool->alloc, rgba);

    mel_gpu_image_init(&entry.texture.image, pool->dev,
        .width = atlas_w,
        .height = atlas_h,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    Mel__Font_MSDF_Upload upload = {
        .image = &entry.texture.image,
        .staging = &staging,
        .width = atlas_w,
        .height = atlas_h,
    };
    mel_gpu_submit_immediate(pool->dev, mel__font_msdf_upload_cmd, &upload);
    mel_gpu_buffer_shutdown(&staging, pool->dev);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VkResult result = vkCreateSampler(pool->dev->device, &sampler_info, nullptr, &entry.texture.sampler);
    assert(result == VK_SUCCESS);
    MEL_UNUSED(result);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, mel_slotmap_handle_to_ptr(sm_handle));

    SDL_Log("font.msdf: loaded '%.*s' %.0fpx, atlas %ux%u", (int)opt.path.len, opt.path.data, font_size, atlas_w, atlas_h);
    return (Mel_Font_Handle){ .handle = sm_handle };
}

Mel_Font_MSDF_Entry* mel_font_msdf_pool_get(Mel_Font_MSDF_Pool* pool, Mel_Font_Handle handle)
{
    assert(pool != nullptr);
    return mel_slotmap_get(&pool->slotmap, handle.handle);
}

Mel_Gpu_Texture* mel_font_msdf_pool_get_texture(Mel_Font_MSDF_Pool* pool, Mel_Font_Handle handle)
{
    Mel_Font_MSDF_Entry* entry = mel_font_msdf_pool_get(pool, handle);
    return entry ? &entry->texture : nullptr;
}

Mel_Vec2 mel_font_msdf_measure_text(Mel_Font_MSDF_Pool* pool, Mel_Font_Handle handle, str8 text)
{
    Mel_Font_MSDF_Entry* entry = mel_font_msdf_pool_get(pool, handle);
    assert(entry != nullptr);
    Mel_Vec2 out = {0};
    mel__font_msdf_measure_desc(&entry->desc, text, &out);
    return out;
}
