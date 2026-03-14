#include "font.atlas.h"
#include "render.list.h"
#include "sprite.pass.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "hash.xxh.h"
#include "gpu.buffer.h"
#include "gpu.submit.h"
#include "allocator.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"

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

static u64 mel__font_atlas_load_key(Mel_Font_Atlas_Load_Opt opt)
{
    u64 key = str8_hash(opt.path);
    f32 font_size = opt.size > 0 ? opt.size : 24.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 512;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 512;

    key = mel_xxh64(&font_size, sizeof(font_size), key);
    key = mel_xxh64(&atlas_w, sizeof(atlas_w), key);
    key = mel_xxh64(&atlas_h, sizeof(atlas_h), key);
    return key;
}

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

void mel_font_atlas_pool_init_opt(Mel_Font_Atlas_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev, Mel_Vfs* vfs, Mel_Font_Atlas_Pool_Init_Opt opt)
{
    assert(pool != nullptr);
    assert(alloc != nullptr);
    assert(dev != nullptr);
    // ASYNC_V2: VFS removed — vfs may be NULL

    *pool = (Mel_Font_Atlas_Pool){0};
    pool->alloc = alloc;
    pool->dev = dev;
    pool->vfs = vfs;
    pool->texture_pool = opt.texture_pool;

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

    u64 hash = mel__font_atlas_load_key(opt);

    void* existing = mel_hashmap_get(&pool->path_to_handle, (void*)(usize)hash);
    if (existing)
    {
        Mel_Font_Handle h = { .handle = mel_slotmap_handle_from_ptr(existing) };
        return h;
    }

    // ASYNC_V2: VFS removed
    SDL_Log("font.atlas: VFS removed, cannot load '%.*s'", (int)opt.path.len, opt.path.data);
    return MEL_FONT_HANDLE_NULL;
}

Mel_Font_Atlas_Entry* mel_font_atlas_pool_get(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle)
{
    assert(pool != nullptr);

    Mel_SlotMap_Handle sm_handle = handle.handle;
    return mel_slotmap_get(&pool->slotmap, sm_handle);
}

Mel_Gpu_Texture* mel_font_atlas_pool_get_texture(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_pool_get(pool, handle);
    return entry ? &entry->atlas_texture : nullptr;
}

Mel_Texture_Handle mel_font_atlas_pool_tex_handle(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_pool_get(pool, handle);
    assert(entry != nullptr);
    return entry->tex_handle;
}

void mel_font_atlas_draw_text_ex(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle,
    Mel_Render_List* list, str8 text, f32 x, f32 y, Mel_Vec4 color, u64 sort_key)
{
    assert(pool != nullptr);
    assert(list != nullptr);

    Mel_Font_Atlas_Entry* entry = mel_font_atlas_pool_get(pool, handle);
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

Mel_Vec2 mel_font_atlas_measure_text(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle, str8 text)
{
    assert(pool != nullptr);

    Mel_Font_Atlas_Entry* entry = mel_font_atlas_pool_get(pool, handle);
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
