#include "font.msdf.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include "allocator.h"
#include "math.scalar.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"

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

void mel_font_msdf_pool_init(Mel_Font_MSDF_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev, Mel_Vfs* vfs)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);
    assert(vfs != nullptr);

    *pool = (Mel_Font_MSDF_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;
    pool->vfs = vfs;

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

    // ASYNC_V2: VFS removed
    SDL_Log("font.msdf: VFS removed, cannot load '%.*s'", (int)opt.path.len, opt.path.data);
    return MEL_FONT_HANDLE_NULL;
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
