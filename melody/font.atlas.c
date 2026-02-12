#include "font.atlas.h"
#include "sprite_batch.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <SDL3/SDL.h>
#include <stb_truetype.h>
#include <string.h>

#define MEL_FONT_ATLAS_FIRST_CHAR 32
#define MEL_FONT_ATLAS_CHAR_COUNT 96

static u64 mel__font_atlas_pool_hash_key(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

static bool mel__font_atlas_pool_eq_key(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

typedef struct {
    Mel_Gpu_Image* image;
    Mel_Gpu_Buffer* staging;
    u32 width;
    u32 height;
} Mel__Font_Upload;

static void mel__font_upload_cmd(VkCommandBuffer cmd, void* user)
{
    Mel__Font_Upload* data = (Mel__Font_Upload*)user;

    mel_gpu_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { data->width, data->height, 1 },
    };

    vkCmdCopyBufferToImage(cmd, data->staging->buffer, data->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    mel_gpu_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void mel_font_atlas_pool_init(Mel_Font_Atlas_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);

    *pool = (Mel_Font_Atlas_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;

    mel_slotmap_init(&pool->slotmap, alloc, .item_size = sizeof(Mel_Font_Atlas_Entry), .initial_capacity = 8);
    mel_hashmap_init(&pool->path_to_handle, mel__font_atlas_pool_hash_key, mel__font_atlas_pool_eq_key, alloc);
}

void mel_font_atlas_pool_shutdown(Mel_Font_Atlas_Pool* pool)
{
    assert(pool != nullptr);

    Mel_Font_Atlas_Entry* entries = mel_slotmap_data(&pool->slotmap);
    u32 count = mel_slotmap_count(&pool->slotmap);

    for (u32 i = 0; i < count; i++)
    {
        mel_gpu_texture_shutdown(&entries[i].atlas_texture, pool->dev);
        if (entries[i].desc.glyphs)
            mel_dealloc(pool->alloc, entries[i].desc.glyphs);
    }

    mel_slotmap_free(&pool->slotmap);
    mel_hashmap_free(&pool->path_to_handle);
    *pool = (Mel_Font_Atlas_Pool){0};
}

Mel_Font_Handle mel_font_atlas_pool_load_opt(Mel_Font_Atlas_Pool* pool, Mel_Font_Atlas_Load_Opt opt)
{
    assert(pool != nullptr);
    assert(!str8_is_empty(opt.path));

    u64 hash = str8_hash(opt.path);

    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
    {
        Mel_Font_Handle h = { .value = (u32)(usize)existing };
        return h;
    }

    char path_buf[1024];
    str8_to_buf(opt.path, path_buf, sizeof(path_buf));

    SDL_IOStream* io = SDL_IOFromFile(path_buf, "rb");
    if (!io)
    {
        SDL_Log("font.atlas: failed to open '%.*s'", (int)opt.path.len, opt.path.data);
        return MEL_FONT_HANDLE_NULL;
    }

    i64 file_size = SDL_GetIOSize(io);
    if (file_size <= 0)
    {
        SDL_CloseIO(io);
        return MEL_FONT_HANDLE_NULL;
    }

    u8* ttf_data = (u8*)mel_alloc(mel_alloc_heap(), (usize)file_size);
    SDL_ReadIO(io, ttf_data, (usize)file_size);
    SDL_CloseIO(io);

    f32 font_size = opt.size > 0 ? opt.size : 24.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 512;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 512;

    u8* bitmap = (u8*)mel_calloc(mel_alloc_heap(), atlas_w * atlas_h);

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_data, 0))
    {
        mel_dealloc(mel_alloc_heap(), bitmap);
        mel_dealloc(mel_alloc_heap(), ttf_data);
        return MEL_FONT_HANDLE_NULL;
    }

    f32 scale = stbtt_ScaleForPixelHeight(&info, font_size);

    int ascent_i, descent_i, line_gap_i;
    stbtt_GetFontVMetrics(&info, &ascent_i, &descent_i, &line_gap_i);

    Mel_Font_Atlas_Entry entry = {0};
    entry.atlas_width = atlas_w;
    entry.atlas_height = atlas_h;

    u32 glyph_count = MEL_FONT_ATLAS_CHAR_COUNT;
    u32 first_codepoint = MEL_FONT_ATLAS_FIRST_CHAR;

    entry.desc.glyphs = mel_alloc_array(pool->alloc, Mel_Font_Glyph, glyph_count);
    entry.desc.glyph_count = glyph_count;
    entry.desc.first_codepoint = first_codepoint;
    entry.desc.ascent = (f32)ascent_i * scale;
    entry.desc.line_height = (f32)(ascent_i - descent_i + line_gap_i) * scale;

    f32 x = 2;
    f32 y = 2;
    f32 row_height = 0;

    for (u32 c = first_codepoint; c < first_codepoint + glyph_count; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&info, (int)c);

        int advance, lsb;
        stbtt_GetGlyphHMetrics(&info, glyph_idx, &advance, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&info, glyph_idx, scale, scale, &x0, &y0, &x1, &y1);

        int gw = x1 - x0;
        int gh = y1 - y0;

        if (x + gw + 2 > atlas_w)
        {
            x = 2;
            y += row_height + 2;
            row_height = 0;
        }

        if (y + gh + 2 > atlas_h)
        {
            mel_dealloc(pool->alloc, entry.desc.glyphs);
            mel_dealloc(mel_alloc_heap(), bitmap);
            mel_dealloc(mel_alloc_heap(), ttf_data);
            return MEL_FONT_HANDLE_NULL;
        }

        if (gw > 0 && gh > 0)
        {
            stbtt_MakeGlyphBitmap(&info, bitmap + (int)y * (int)atlas_w + (int)x,
                gw, gh, (int)atlas_w, scale, scale, glyph_idx);
        }

        u32 idx = c - first_codepoint;
        entry.desc.glyphs[idx].x0 = (f32)x0;
        entry.desc.glyphs[idx].y0 = (f32)y0;
        entry.desc.glyphs[idx].x1 = (f32)x1;
        entry.desc.glyphs[idx].y1 = (f32)y1;
        entry.desc.glyphs[idx].u0 = x / (f32)atlas_w;
        entry.desc.glyphs[idx].v0 = y / (f32)atlas_h;
        entry.desc.glyphs[idx].u1 = (x + gw) / (f32)atlas_w;
        entry.desc.glyphs[idx].v1 = (y + gh) / (f32)atlas_h;
        entry.desc.glyphs[idx].xadvance = (f32)advance * scale;

        if ((f32)gh > row_height) row_height = (f32)gh;
        x += gw + 2;
    }

    u8* rgba = (u8*)mel_alloc(mel_alloc_heap(), atlas_w * atlas_h * 4);
    for (u32 i = 0; i < atlas_w * atlas_h; i++)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap[i];
    }

    mel_dealloc(mel_alloc_heap(), bitmap);
    mel_dealloc(mel_alloc_heap(), ttf_data);

    Mel_Gpu_Buffer staging;
    u32 image_size = atlas_w * atlas_h * 4;

    mel_gpu_buffer_init(&staging, pool->dev,
        .size = image_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_ONLY);

    mel_gpu_buffer_upload(&staging, pool->dev, rgba, image_size, 0);
    mel_dealloc(mel_alloc_heap(), rgba);

    mel_gpu_image_init(&entry.atlas_texture.image, pool->dev,
        .width = atlas_w,
        .height = atlas_h,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    Mel__Font_Upload upload = {
        .image = &entry.atlas_texture.image,
        .staging = &staging,
        .width = atlas_w,
        .height = atlas_h,
    };

    mel_gpu_submit_immediate(pool->dev, mel__font_upload_cmd, &upload);
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

    VkResult result = vkCreateSampler(pool->dev->device, &sampler_info, nullptr, &entry.atlas_texture.sampler);
    assert(result == VK_SUCCESS);
    MEL_UNUSED(result);

    Mel_SlotMap_Handle sm_handle = mel_slotmap_insert(&pool->slotmap, &entry);
    Mel_Font_Handle fh = { .value = sm_handle.value };
    mel_hashmap_put(&pool->path_to_handle, (void*)(usize)hash, (void*)(usize)fh.value);

    SDL_Log("font.atlas: loaded '%.*s' %.0fpx, atlas %ux%u", (int)opt.path.len, opt.path.data, font_size, atlas_w, atlas_h);

    return fh;
}

Mel_Font_Atlas_Entry* mel_font_atlas_pool_get(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = { .value = handle.value };
    return mel_slotmap_get(&pool->slotmap, sm_handle);
}

void mel_font_atlas_draw_text(Mel_Font_Atlas_Entry* entry, Mel_SpriteBatch* batch, str8 text, f32 x, f32 y, Mel_Vec4 color)
{
    assert(entry != nullptr);
    assert(batch != nullptr);

    if (str8_is_empty(text)) return;

    batch->current_texture = &entry->atlas_texture;
    batch->current_descriptor = VK_NULL_HANDLE;

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
            mel_sprite_batch_draw_uv(batch, gx, gy, gw, gh, g->u0, g->v0, g->u1, g->v1, color);

        cursor_x += g->xadvance;
    }
}

Mel_Vec2 mel_font_atlas_measure_text(Mel_Font_Atlas_Entry* entry, str8 text)
{
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
