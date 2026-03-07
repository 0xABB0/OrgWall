#include "mugen_sff.h"
#include "sprite.sheet.h"
#include "vfs.h"
#include "allocator.h"
#include "string.str8.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <assert.h>

typedef struct {
    u32 next_header_offset;
    u32 data_size;
    i16 offset_x;
    i16 offset_y;
    u16 group;
    u16 number;
    u16 link_index;
    u8 shared_palette;
} Sff_V1_Sprite_Header;

typedef struct {
    u16 width;
    u16 height;
    u16 bytes_per_line;
    u8 encoding;
} Pcx_Info;

static u16 read_u16(const u8* p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 read_u32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static i16 read_i16(const u8* p) { return (i16)read_u16(p); }

static bool parse_file_header(const u8* data, usize size, u32* out_sprite_count, u32* out_first_header_offset)
{
    if (size < 32) return false;
    if (memcmp(data, "ElecbyteSpr\0", 12) != 0) return false;

    u8 ver_hi = data[15];
    if (ver_hi != 1)
    {
        SDL_Log("SFF: only V1 supported, got V%u", ver_hi);
        return false;
    }

    *out_sprite_count = read_u32(data + 20);
    *out_first_header_offset = read_u32(data + 24);
    return true;
}

static bool parse_sprite_header(const u8* data, usize file_size, u32 offset, Sff_V1_Sprite_Header* out)
{
    if (offset + 32 > file_size) return false;
    const u8* p = data + offset;

    out->next_header_offset = read_u32(p + 0);
    out->data_size = read_u32(p + 4);
    out->offset_x = read_i16(p + 8);
    out->offset_y = read_i16(p + 10);
    out->group = read_u16(p + 12);
    out->number = read_u16(p + 14);
    out->link_index = read_u16(p + 16);

    if (offset + 32 < file_size)
        out->shared_palette = data[offset + 32];
    else
        out->shared_palette = 0;

    return true;
}

static bool parse_pcx_header(const u8* data, usize file_size, u32 offset, Pcx_Info* out)
{
    if (offset + 128 > file_size) return false;
    const u8* p = data + offset;

    out->encoding = p[2];
    u8 bpp = p[3];
    if (bpp != 8) return false;

    u16 x_min = read_u16(p + 4);
    u16 y_min = read_u16(p + 6);
    u16 x_max = read_u16(p + 8);
    u16 y_max = read_u16(p + 10);

    out->width = x_max - x_min + 1;
    out->height = y_max - y_min + 1;
    out->bytes_per_line = read_u16(p + 66);

    return true;
}

static u8* rle_pcx_decode(const u8* rle, usize rle_size, u16 width, u16 height, u16 bytes_per_line)
{
    usize pixel_count = (usize)width * (usize)height;
    u8* pixels = malloc(pixel_count);
    if (!pixels) return nullptr;
    memset(pixels, 0, pixel_count);

    usize i = 0;
    usize j = 0;
    u32 col = 0;

    while (j < pixel_count && i < rle_size)
    {
        u8 byte = rle[i++];
        u32 run = 1;
        u8 value = byte;

        if (byte >= 0xC0)
        {
            run = byte & 0x3F;
            if (i < rle_size)
                value = rle[i++];
            else
                value = 0;
        }

        for (u32 r = 0; r < run; r++)
        {
            if (col < width && j < pixel_count)
            {
                pixels[j] = value;
                j++;
            }
            col++;
            if (col >= bytes_per_line)
            {
                col = 0;
                run = 1;
            }
        }
    }

    return pixels;
}

static i64 find_palette_marker(const u8* data, usize file_size, u32 pcx_data_start, u32 block_end)
{
    if (block_end < 769 || block_end > file_size) return -1;

    for (i64 pos = (i64)block_end - 769; pos >= (i64)pcx_data_start; pos--)
    {
        if (data[pos] == 0x0C)
            return pos;
    }

    return (i64)block_end - 769;
}

static void read_palette(const u8* data, i64 marker_offset, u32 palette[256])
{
    const u8* p = data + marker_offset + 1;
    for (u32 i = 0; i < 256; i++)
    {
        u8 r = p[i * 3 + 0];
        u8 g = p[i * 3 + 1];
        u8 b = p[i * 3 + 2];
        u8 a = (i == 0) ? 0 : 255;
        palette[i] = ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | (u32)r;
    }
}

typedef struct {
    u8* pixels;
    u16 width;
    u16 height;
    i16 offset_x;
    i16 offset_y;
    u16 group;
    u16 number;
} Decoded_Sprite;

static void pack_atlas(Decoded_Sprite* sprites, u32 count, u32* out_width, u32* out_height,
                        Mel_SpriteFrame* frames)
{
    u32 max_w = 2048;
    u32 x = 0;
    u32 y = 0;
    u32 row_height = 0;
    u32 total_w = 0;

    for (u32 i = 0; i < count; i++)
    {
        u16 w = sprites[i].width;
        u16 h = sprites[i].height;

        if (x + w > max_w)
        {
            y += row_height;
            x = 0;
            row_height = 0;
        }

        frames[i].x = x;
        frames[i].y = y;
        frames[i].width = w;
        frames[i].height = h;
        frames[i].offset_x = sprites[i].offset_x;
        frames[i].offset_y = sprites[i].offset_y;

        if (x + w > total_w) total_w = x + w;
        if (h > row_height) row_height = h;
        x += w;
    }

    y += row_height;

    u32 pot_w = 1;
    while (pot_w < total_w) pot_w <<= 1;
    u32 pot_h = 1;
    while (pot_h < y) pot_h <<= 1;

    *out_width = pot_w;
    *out_height = pot_h;
}

static void blit_to_atlas(u8* atlas, u32 atlas_w, const u8* indexed_pixels,
                          u16 sprite_w, u16 sprite_h, u32 dst_x, u32 dst_y,
                          const u32 palette[256])
{
    for (u32 row = 0; row < sprite_h; row++)
    {
        for (u32 col = 0; col < sprite_w; col++)
        {
            u8 idx = indexed_pixels[row * sprite_w + col];
            u32 rgba = palette[idx];
            u32 dst = ((dst_y + row) * atlas_w + (dst_x + col)) * 4;
            atlas[dst + 0] = (u8)(rgba & 0xFF);
            atlas[dst + 1] = (u8)((rgba >> 8) & 0xFF);
            atlas[dst + 2] = (u8)((rgba >> 16) & 0xFF);
            atlas[dst + 3] = (u8)((rgba >> 24) & 0xFF);
        }
    }
}

bool mugen_sff_load(Mugen_Sff* sff, Mel_Vfs* vfs, str8 path, const Mel_Alloc* alloc)
{
    assert(sff);
    assert(vfs);
    assert(alloc);

    *sff = (Mugen_Sff){0};

    usize file_size = 0;
    u8* data = mel_vfs_read_file_alloc(vfs, path, &file_size, alloc);
    if (!data)
    {
        SDL_Log("SFF: failed to read %.*s", (int)path.len, path.data);
        return false;
    }

    u32 sprite_count = 0;
    u32 first_header = 0;
    if (!parse_file_header(data, file_size, &sprite_count, &first_header))
    {
        SDL_Log("SFF: invalid header in %.*s", (int)path.len, path.data);
        mel_dealloc(alloc, data);
        return false;
    }

    Decoded_Sprite* decoded = malloc(sprite_count * sizeof(Decoded_Sprite));
    u32 palette[256] = {0};
    bool have_palette = false;
    u32 valid_count = 0;

    u32 header_offset = first_header;
    for (u32 i = 0; i < sprite_count; i++)
    {
        Sff_V1_Sprite_Header sh;
        if (!parse_sprite_header(data, file_size, header_offset, &sh))
            break;

        u32 pcx_offset = header_offset + 32;

        if (sh.data_size == 0)
        {
            if (sh.link_index < valid_count)
            {
                Decoded_Sprite* src = &decoded[sh.link_index];
                Decoded_Sprite* dst = &decoded[valid_count];
                usize px_size = (usize)src->width * (usize)src->height;
                dst->pixels = malloc(px_size);
                memcpy(dst->pixels, src->pixels, px_size);
                dst->width = src->width;
                dst->height = src->height;
                dst->offset_x = sh.offset_x;
                dst->offset_y = sh.offset_y;
                dst->group = sh.group;
                dst->number = sh.number;
                valid_count++;
            }
            header_offset = sh.next_header_offset;
            continue;
        }

        Pcx_Info pcx;
        if (!parse_pcx_header(data, file_size, pcx_offset, &pcx))
        {
            header_offset = sh.next_header_offset;
            continue;
        }

        u32 pcx_data_start = pcx_offset + 128;
        u32 block_end = pcx_offset + sh.data_size;

        if (!have_palette || (i > 0 && sh.shared_palette == 0))
        {
            i64 pal_marker = find_palette_marker(data, file_size, pcx_data_start, block_end);
            if (pal_marker >= 0 && (usize)(pal_marker + 769) <= file_size)
            {
                read_palette(data, pal_marker, palette);
                have_palette = true;
            }
        }

        i64 pal_marker = find_palette_marker(data, file_size, pcx_data_start, block_end);
        usize rle_size = (pal_marker >= 0)
            ? (usize)(pal_marker - pcx_data_start)
            : (usize)(block_end - pcx_data_start);

        if (pcx_data_start + rle_size > file_size)
        {
            header_offset = sh.next_header_offset;
            continue;
        }

        u8* indexed = nullptr;
        if (pcx.encoding == 1)
            indexed = rle_pcx_decode(data + pcx_data_start, rle_size, pcx.width, pcx.height, pcx.bytes_per_line);
        else
        {
            usize px_count = (usize)pcx.width * (usize)pcx.height;
            indexed = malloc(px_count);
            if (rle_size >= px_count)
                memcpy(indexed, data + pcx_data_start, px_count);
            else
                memset(indexed, 0, px_count);
        }

        if (!indexed)
        {
            header_offset = sh.next_header_offset;
            continue;
        }

        decoded[valid_count] = (Decoded_Sprite){
            .pixels = indexed,
            .width = pcx.width,
            .height = pcx.height,
            .offset_x = sh.offset_x,
            .offset_y = sh.offset_y,
            .group = sh.group,
            .number = sh.number,
        };
        valid_count++;
        header_offset = sh.next_header_offset;
    }

    mel_dealloc(alloc, data);

    if (valid_count == 0)
    {
        free(decoded);
        SDL_Log("SFF: no valid sprites in %.*s", (int)path.len, path.data);
        return false;
    }

    sff->sheet = (Mel_Spritesheet){0};
    sff->sheet.alloc = alloc;
    sff->sheet.frame_count = valid_count;
    sff->sheet.frames = mel_calloc(alloc, valid_count * sizeof(Mel_SpriteFrame));

    pack_atlas(decoded, valid_count, &sff->atlas_width, &sff->atlas_height, sff->sheet.frames);

    sff->sheet.texture_width = sff->atlas_width;
    sff->sheet.texture_height = sff->atlas_height;

    usize atlas_size = (usize)sff->atlas_width * (usize)sff->atlas_height * 4;
    sff->atlas_pixels = mel_calloc(alloc, atlas_size);

    for (u32 i = 0; i < valid_count; i++)
    {
        blit_to_atlas(sff->atlas_pixels, sff->atlas_width,
                      decoded[i].pixels, decoded[i].width, decoded[i].height,
                      sff->sheet.frames[i].x, sff->sheet.frames[i].y,
                      palette);
    }

    sff->entries = mel_calloc(alloc, valid_count * sizeof(Mugen_Sff_Entry));
    sff->entry_count = valid_count;
    for (u32 i = 0; i < valid_count; i++)
    {
        sff->entries[i] = (Mugen_Sff_Entry){
            .group = decoded[i].group,
            .number = decoded[i].number,
            .frame_index = i,
        };
    }

    for (u32 i = 0; i < valid_count; i++)
        free(decoded[i].pixels);
    free(decoded);

    SDL_Log("SFF: loaded %u sprites from %.*s (atlas %ux%u)",
            valid_count, (int)path.len, path.data,
            sff->atlas_width, sff->atlas_height);

    return true;
}

u32 mugen_sff_find_frame(Mugen_Sff* sff, u16 group, u16 number)
{
    assert(sff);
    for (u32 i = 0; i < sff->entry_count; i++)
    {
        if (sff->entries[i].group == group && sff->entries[i].number == number)
            return sff->entries[i].frame_index;
    }
    return 0;
}

void mugen_sff_shutdown(Mugen_Sff* sff, const Mel_Alloc* alloc)
{
    assert(sff);
    assert(alloc);

    if (sff->atlas_pixels)
        mel_dealloc(alloc, sff->atlas_pixels);

    if (sff->entries)
        mel_dealloc(alloc, sff->entries);

    mel_spritesheet_free(&sff->sheet);

    *sff = (Mugen_Sff){0};
}
