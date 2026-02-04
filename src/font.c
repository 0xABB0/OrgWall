#include "font.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "memory.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

typedef struct
{
    Mel_VkImage* image;
    Mel_VkBuffer* staging;
    u32 width;
    u32 height;
} FontUploadData;

static void font_upload_cmd(VkCommandBuffer cmd, void* user)
{
    FontUploadData* data = (FontUploadData*)user;

    mel_vk_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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

    mel_vk_image_transition(data->image, cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

bool mel_font_init_opt(Mel_Font* font, Mel_VkContext* ctx, Mel_Font_Opt opt)
{
    assert(font != nullptr);
    assert(ctx != nullptr);
    assert(opt.path != nullptr || opt.data != nullptr);

    *font = (Mel_Font){0};

    u8* ttf_data = nullptr;
    bool owns_data = false;

    if (opt.path)
    {
        SDL_IOStream* io = SDL_IOFromFile(opt.path, "rb");
        if (!io)
        {
            SDL_Log("Failed to open font file: %s", opt.path);
            return false;
        }

        i64 size = SDL_GetIOSize(io);
        if (size <= 0)
        {
            SDL_CloseIO(io);
            return false;
        }

        ttf_data = (u8*)mel_malloc(mel_alloc_malloc(), (size_t)size);
        if (!ttf_data)
        {
            SDL_CloseIO(io);
            return false;
        }

        SDL_ReadIO(io, ttf_data, (size_t)size);
        SDL_CloseIO(io);
        owns_data = true;
    }
    else
    {
        ttf_data = (u8*)opt.data;
    }

    f32 font_size = opt.size > 0 ? opt.size : 24.0f;
    u32 atlas_w = opt.atlas_width > 0 ? opt.atlas_width : 512;
    u32 atlas_h = opt.atlas_height > 0 ? opt.atlas_height : 512;

    font->atlas_width = atlas_w;
    font->atlas_height = atlas_h;

    u8* bitmap = (u8*)mel_calloc(mel_alloc_malloc(), atlas_w * atlas_h);
    if (!bitmap)
    {
        if (owns_data) mel_free(mel_alloc_malloc(), ttf_data);
        return false;
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_data, 0))
    {
        SDL_Log("Failed to init font");
        mel_free(mel_alloc_malloc(), bitmap);
        if (owns_data) mel_free(mel_alloc_malloc(), ttf_data);
        return false;
    }

    f32 scale = stbtt_ScaleForPixelHeight(&info, font_size);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    font->ascent = (f32)ascent * scale;
    font->line_height = (f32)(ascent - descent + line_gap) * scale;

    f32 x = 2;
    f32 y = 2;
    f32 row_height = 0;

    for (int c = MEL_FONT_FIRST_CHAR; c < MEL_FONT_FIRST_CHAR + MEL_FONT_CHAR_COUNT; c++)
    {
        int glyph_idx = stbtt_FindGlyphIndex(&info, c);

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
            SDL_Log("Font atlas too small!");
            mel_free(mel_alloc_malloc(), bitmap);
            if (owns_data) mel_free(mel_alloc_malloc(), ttf_data);
            return false;
        }

        if (gw > 0 && gh > 0)
        {
            stbtt_MakeGlyphBitmap(&info, bitmap + (int)y * (int)atlas_w + (int)x,
                gw, gh, (int)atlas_w, scale, scale, glyph_idx);
        }

        int idx = c - MEL_FONT_FIRST_CHAR;
        font->glyphs[idx].x0 = (f32)x0;
        font->glyphs[idx].y0 = (f32)y0;
        font->glyphs[idx].x1 = (f32)x1;
        font->glyphs[idx].y1 = (f32)y1;
        font->glyphs[idx].u0 = x / (f32)atlas_w;
        font->glyphs[idx].v0 = y / (f32)atlas_h;
        font->glyphs[idx].u1 = (x + gw) / (f32)atlas_w;
        font->glyphs[idx].v1 = (y + gh) / (f32)atlas_h;
        font->glyphs[idx].xadvance = (f32)advance * scale;

        if ((f32)gh > row_height) row_height = (f32)gh;
        x += gw + 2;
    }

    u8* rgba = (u8*)mel_malloc(mel_alloc_malloc(), atlas_w * atlas_h * 4);
    if (!rgba)
    {
        mel_free(mel_alloc_malloc(), bitmap);
        if (owns_data) mel_free(mel_alloc_malloc(), ttf_data);
        return false;
    }

    for (u32 i = 0; i < atlas_w * atlas_h; i++)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap[i];
    }

    mel_free(mel_alloc_malloc(), bitmap);
    if (owns_data) mel_free(mel_alloc_malloc(), ttf_data);

    Mel_VkBuffer staging;
    u32 image_size = atlas_w * atlas_h * 4;

    if (!mel_vk_buffer_init(&staging, ctx, image_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY))
    {
        mel_free(mel_alloc_malloc(), rgba);
        return false;
    }

    mel_vk_buffer_upload(&staging, ctx, rgba, image_size, 0);
    mel_free(mel_alloc_malloc(), rgba);

    if (!mel_vk_image_init(&font->atlas.image, ctx,
        .width = atlas_w,
        .height = atlas_h,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT))
    {
        mel_vk_buffer_shutdown(&staging, ctx);
        return false;
    }

    FontUploadData upload = {
        .image = &font->atlas.image,
        .staging = &staging,
        .width = atlas_w,
        .height = atlas_h,
    };

    if (!mel_vk_submit_immediate(ctx, font_upload_cmd, &upload))
    {
        mel_vk_image_shutdown(&font->atlas.image, ctx);
        mel_vk_buffer_shutdown(&staging, ctx);
        return false;
    }

    mel_vk_buffer_shutdown(&staging, ctx);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult result = vkCreateSampler(ctx->device, &sampler_info, nullptr, &font->atlas.sampler);
    if (result != VK_SUCCESS)
    {
        mel_vk_image_shutdown(&font->atlas.image, ctx);
        return false;
    }

    SDL_Log("Font loaded: %.0fpx, atlas %ux%u", font_size, atlas_w, atlas_h);

    return true;
}

void mel_font_shutdown(Mel_Font* font, Mel_VkContext* ctx)
{
    assert(font != nullptr);
    assert(ctx != nullptr);

    mel_vk_texture_shutdown(&font->atlas, ctx);
}

void mel_font_draw_text(Mel_Font* font, Mel_SpriteBatch* batch, const char* text, f32 x, f32 y, Mel_Vec4 color)
{
    assert(font != nullptr);
    assert(batch != nullptr);

    if (!text) return;

    batch->current_texture = &font->atlas;
    batch->current_descriptor = font->atlas.descriptor;

    f32 cursor_x = x;
    f32 cursor_y = y + font->ascent;

    for (const char* p = text; *p; p++)
    {
        int c = (u8)*p;

        if (c == '\n')
        {
            cursor_x = x;
            cursor_y += font->line_height;
            continue;
        }

        if (c < MEL_FONT_FIRST_CHAR || c >= MEL_FONT_FIRST_CHAR + MEL_FONT_CHAR_COUNT)
        {
            continue;
        }

        int idx = c - MEL_FONT_FIRST_CHAR;
        Mel_GlyphInfo* g = &font->glyphs[idx];

        f32 gx = cursor_x + g->x0;
        f32 gy = cursor_y + g->y0;
        f32 gw = g->x1 - g->x0;
        f32 gh = g->y1 - g->y0;

        if (gw > 0 && gh > 0)
        {
            mel_sprite_batch_draw_uv(batch, gx, gy, gw, gh, g->u0, g->v0, g->u1, g->v1, color);
        }

        cursor_x += g->xadvance;
    }
}

Mel_Vec2 mel_font_measure_text(Mel_Font* font, const char* text)
{
    assert(font != nullptr);

    if (!text) return mel_vec2(0, 0);

    f32 max_width = 0;
    f32 width = 0;
    f32 height = font->line_height;

    for (const char* p = text; *p; p++)
    {
        int c = (u8)*p;

        if (c == '\n')
        {
            if (width > max_width) max_width = width;
            width = 0;
            height += font->line_height;
            continue;
        }

        if (c < MEL_FONT_FIRST_CHAR || c >= MEL_FONT_FIRST_CHAR + MEL_FONT_CHAR_COUNT)
        {
            continue;
        }

        int idx = c - MEL_FONT_FIRST_CHAR;
        width += font->glyphs[idx].xadvance;
    }

    if (width > max_width) max_width = width;

    return mel_vec2(max_width, height);
}
