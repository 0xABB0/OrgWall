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

void mel_font_sdf_pool_init(Mel_Font_SDF_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev, Mel_Vfs* vfs)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);
    assert(vfs != nullptr);

    *pool = (Mel_Font_SDF_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;
    pool->vfs = vfs;

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

    usize file_size = 0;
    u8* ttf_data = mel_vfs_read_file_alloc(pool->vfs, opt.path, &file_size, pool->alloc);
    if (!ttf_data)
    {
        SDL_Log("font.sdf: failed to open '%.*s'", (int)opt.path.len, opt.path.data);
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
    f32 scale = stbtt_ScaleForPixelHeight(&info, font_size);

    int ascent_i, descent_i, line_gap_i;
    stbtt_GetFontVMetrics(&info, &ascent_i, &descent_i, &line_gap_i);

    Mel_Font_SDF_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;
    entry.px_range = px_range;
    entry.desc.first_codepoint = MEL_FONT_SDF_FIRST_CHAR;
    entry.desc.glyph_count = MEL_FONT_SDF_CHAR_COUNT;
    entry.desc.glyphs = mel_alloc_array(pool->alloc, Mel_Font_Glyph, entry.desc.glyph_count);
    entry.desc.ascent = (f32)ascent_i * scale;
    entry.desc.line_height = (f32)(ascent_i - descent_i + line_gap_i) * scale;

    u32 x = padding;
    u32 y = padding;
    u32 row_height = 0;

    for (u32 c = MEL_FONT_SDF_FIRST_CHAR; c < MEL_FONT_SDF_FIRST_CHAR + MEL_FONT_SDF_CHAR_COUNT; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&info, (int)c);
        int advance = 0;
        int lsb = 0;
        stbtt_GetGlyphHMetrics(&info, glyph_idx, &advance, &lsb);

        int gw = 0, gh = 0, xoff = 0, yoff = 0;
        f32 pixel_dist_scale = 128.0f / mel_maxf(px_range, 1.0f);
        unsigned char* sdf = stbtt_GetGlyphSDF(&info, scale, glyph_idx, (int)padding, 128, pixel_dist_scale,
            &gw, &gh, &xoff, &yoff);
        u32 idx = c - MEL_FONT_SDF_FIRST_CHAR;
        entry.desc.glyphs[idx].xadvance = (f32)advance * scale;

        if (!sdf)
            continue;

        if (x + (u32)gw + padding > atlas_w)
        {
            x = padding;
            y += row_height + padding;
            row_height = 0;
        }

        if (y + (u32)gh + padding > atlas_h)
        {
            stbtt_FreeSDF(sdf, nullptr);
            mel_dealloc(pool->alloc, entry.desc.glyphs);
            mel_dealloc(pool->alloc, rgba);
            mel_dealloc(pool->alloc, ttf_data);
            return MEL_FONT_HANDLE_NULL;
        }

        for (int row = 0; row < gh; row++)
        {
            for (int col = 0; col < gw; col++)
            {
                u8 v = sdf[row * gw + col];
                usize dst = (((usize)(y + (u32)row) * atlas_w) + (x + (u32)col)) * 4;
                rgba[dst + 0] = v;
                rgba[dst + 1] = v;
                rgba[dst + 2] = v;
                rgba[dst + 3] = v;
            }
        }
        stbtt_FreeSDF(sdf, nullptr);

        entry.desc.glyphs[idx] = (Mel_Font_Glyph){
            .x0 = (f32)xoff,
            .y0 = (f32)yoff,
            .x1 = (f32)(xoff + gw),
            .y1 = (f32)(yoff + gh),
            .u0 = x / (f32)atlas_w,
            .v0 = y / (f32)atlas_h,
            .u1 = (x + (u32)gw) / (f32)atlas_w,
            .v1 = (y + (u32)gh) / (f32)atlas_h,
            .xadvance = entry.desc.glyphs[idx].xadvance,
        };

        if ((u32)gh > row_height) row_height = (u32)gh;
        x += (u32)gw + padding;
    }

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

    Mel__Font_SDF_Upload upload = {
        .image = &entry.texture.image,
        .staging = &staging,
        .width = atlas_w,
        .height = atlas_h,
    };
    mel_gpu_submit_immediate(pool->dev, mel__font_sdf_upload_cmd, &upload);
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

    SDL_Log("font.sdf: loaded '%.*s' %.0fpx, atlas %ux%u", (int)opt.path.len, opt.path.data, font_size, atlas_w, atlas_h);
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
