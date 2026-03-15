#include "mugen.sff.h"
#include "sprite.sheet.h"
#include "math.geo.rect.h"
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
    u16 group;
    u16 number;
    u16 width;
    u16 height;
    i16 offset_x;
    i16 offset_y;
    u16 link_index;
    u8 format;
    u8 coldepth;
    u32 data_offset;
    u32 data_length;
    u16 palette_index;
    u16 flags;
} Sff_V2_Sprite_Header;

typedef struct {
    u16 width;
    u16 height;
    u16 bytes_per_line;
    u8 encoding;
} Pcx_Info;

typedef struct {
    u8* pixels;
    u16 width;
    u16 height;
    i16 offset_x;
    i16 offset_y;
    u16 group;
    u16 number;
} Decoded_Sprite;

typedef struct {
    u32 x, y;
} Pack_Pos;

static u16 read_u16(const u8* p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 read_u32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static i16 read_i16(const u8* p) { return (i16)read_u16(p); }

typedef struct {
    u8 version_hi;
    u32 sprite_count;
    u32 first_sprite_offset;
    u32 palette_count;
    u32 first_palette_offset;
    u32 lofs;
    u32 tofs;
} Sff_File_Header;

static bool parse_file_header(const u8* data, usize size, Sff_File_Header* out)
{
    if (size < 36) return false;
    if (memcmp(data, "ElecbyteSpr\0", 12) != 0) return false;

    out->version_hi = data[15];

    if (out->version_hi == 1)
    {
        if (size < 32) return false;
        out->sprite_count = read_u32(data + 20);
        out->first_sprite_offset = read_u32(data + 24);
        out->palette_count = 0;
        out->first_palette_offset = 0;
        out->lofs = 0;
        out->tofs = 0;
        return true;
    }
    else if (out->version_hi == 2)
    {
        if (size < 64) return false;
        out->first_sprite_offset = read_u32(data + 36);
        out->sprite_count = read_u32(data + 40);
        out->first_palette_offset = read_u32(data + 44);
        out->palette_count = read_u32(data + 48);
        out->lofs = read_u32(data + 52);
        out->tofs = read_u32(data + 60);
        return true;
    }

    SDL_Log("SFF: unsupported version %u", out->version_hi);
    return false;
}

static bool parse_v1_sprite_header(const u8* data, usize file_size, u32 offset, Sff_V1_Sprite_Header* out)
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
    out->shared_palette = p[18];

    return true;
}

static bool parse_v2_sprite_header(const u8* data, usize file_size, u32 offset, Sff_V2_Sprite_Header* out)
{
    if (offset + 28 > file_size) return false;
    const u8* p = data + offset;

    out->group = read_u16(p + 0);
    out->number = read_u16(p + 2);
    out->width = read_u16(p + 4);
    out->height = read_u16(p + 6);
    out->offset_x = read_i16(p + 8);
    out->offset_y = read_i16(p + 10);
    out->link_index = read_u16(p + 12);
    out->format = p[14];
    out->coldepth = p[15];
    out->data_offset = read_u32(p + 16);
    out->data_length = read_u32(p + 20);
    out->palette_index = read_u16(p + 24);
    out->flags = read_u16(p + 26);

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

    usize src = 0;
    usize dst = 0;
    u32 col = 0;

    while (dst < pixel_count && src < rle_size)
    {
        u8 byte = rle[src++];
        u32 run = 1;
        u8 value = byte;

        if (byte >= 0xC0)
        {
            run = byte & 0x3F;
            if (src < rle_size)
                value = rle[src++];
            else
                value = 0;
        }

        for (u32 r = 0; r < run; r++)
        {
            if (col < width && dst < pixel_count)
            {
                pixels[dst] = value;
                dst++;
            }
            col++;
            if (col >= bytes_per_line)
                col = 0;
        }
    }

    return pixels;
}

static u8* rle8_decode(const u8* rle, usize rle_size, u32 pixel_count)
{
    u8* p = malloc(pixel_count);
    if (!p) return nullptr;
    memset(p, 0, pixel_count);

    usize i = 0, j = 0;
    while (j < pixel_count && i < rle_size)
    {
        u32 n = 1;
        u8 d = rle[i++];
        if ((d & 0xc0) == 0x40)
        {
            n = d & 0x3f;
            if (i < rle_size)
                d = rle[i++];
            else
                d = 0;
        }
        for (u32 k = 0; k < n && j < pixel_count; k++)
            p[j++] = d;
    }
    return p;
}

static u8* rle5_decode(const u8* rle, usize rle_size, u32 pixel_count)
{
    u8* p = malloc(pixel_count);
    if (!p) return nullptr;
    memset(p, 0, pixel_count);

    usize i = 0;
    u32 j = 0;
    while (j < pixel_count && i < rle_size)
    {
        i32 rl = (i32)rle[i];
        if (i < rle_size - 1) i++;
        i32 dl = (i32)(rle[i] & 0x7f);
        u8 c = 0;
        if (rle[i] >> 7)
        {
            if (i < rle_size - 1) i++;
            c = rle[i];
        }
        if (i < rle_size - 1) i++;

        for (;;)
        {
            if (j < pixel_count)
                p[j++] = c;
            rl--;
            if (rl < 0)
            {
                dl--;
                if (dl < 0) break;
                c = rle[i] & 0x1f;
                rl = (i32)(rle[i] >> 5);
                if (i < rle_size - 1) i++;
            }
        }
    }
    return p;
}

static u8* lz5_decode(const u8* rle, usize rle_size, u32 pixel_count)
{
    u8* p = malloc(pixel_count);
    if (!p) return nullptr;
    memset(p, 0, pixel_count);

    if (rle_size == 0) return p;

    usize i = 0;
    u32 j = 0;
    u8 ct = rle[i];
    if (i < rle_size - 1) i++;
    u32 cts = 0;
    u8 rb = 0;
    u32 rbc = 0;

    while (j < pixel_count && i < rle_size)
    {
        i32 d = (i32)rle[i];
        if (i < rle_size - 1) i++;

        if (ct & (1u << cts))
        {
            i32 n;
            if ((d & 0x3f) == 0)
            {
                d = (d << 2 | (i32)rle[i]) + 1;
                if (i < rle_size - 1) i++;
                n = (i32)rle[i] + 2;
                if (i < rle_size - 1) i++;
            }
            else
            {
                rb |= (u8)((d & 0xc0) >> rbc);
                rbc += 2;
                n = d & 0x3f;
                if (rbc < 8)
                {
                    d = (i32)rle[i] + 1;
                    if (i < rle_size - 1) i++;
                }
                else
                {
                    d = (i32)rb + 1;
                    rb = 0;
                    rbc = 0;
                }
            }
            for (; n >= 0 && j < pixel_count; n--)
            {
                p[j] = (j >= (u32)d) ? p[j - d] : 0;
                j++;
            }
        }
        else
        {
            i32 n;
            if ((d & 0xe0) == 0)
            {
                n = (i32)rle[i] + 8;
                if (i < rle_size - 1) i++;
            }
            else
            {
                n = d >> 5;
                d &= 0x1f;
            }
            for (; n > 0 && j < pixel_count; n--)
                p[j++] = (u8)d;
        }

        cts++;
        if (cts >= 8)
        {
            ct = rle[i];
            cts = 0;
            if (i < rle_size - 1) i++;
        }
    }
    return p;
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

static void read_v1_palette(const u8* data, i64 marker_offset, u32 palette[256])
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

static void read_v2_palette(const u8* data, usize file_size, u32 offset, u32 size, u32 palette[256])
{
    u32 raw_count = size / 4;
    if (raw_count > 256) raw_count = 256;

    for (u32 i = 0; i < 256; i++)
    {
        if (i < raw_count && offset + i * 4 + 4 <= file_size)
        {
            const u8* p = data + offset + i * 4;
            u8 r = p[0];
            u8 g = p[1];
            u8 b = p[2];
            u8 a = (i == 0) ? 0 : 255;
            palette[i] = ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | (u32)r;
        }
        else
        {
            palette[i] = 0;
        }
    }
}

static void pack_atlas(Decoded_Sprite* sprites, u32 count, u32* out_width, u32* out_height,
                        Pack_Pos* positions)
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

        positions[i].x = x;
        positions[i].y = y;

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

static void blit_indexed_to_atlas(u8* atlas, u32 atlas_w, const u8* indexed_pixels,
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

static void blit_rgba_to_atlas(u8* atlas, u32 atlas_w, const u8* rgba_pixels,
                                u16 sprite_w, u16 sprite_h, u32 dst_x, u32 dst_y)
{
    for (u32 row = 0; row < sprite_h; row++)
    {
        u32 src_off = row * sprite_w * 4;
        u32 dst_off = ((dst_y + row) * atlas_w + dst_x) * 4;
        memcpy(atlas + dst_off, rgba_pixels + src_off, sprite_w * 4);
    }
}

static bool load_v1(Mugen_Sff* sff, const u8* data, usize file_size,
                     Sff_File_Header* fh, const Mel_Alloc* alloc)
{
    Decoded_Sprite* decoded = malloc(fh->sprite_count * sizeof(Decoded_Sprite));
    u32 (*sprite_palettes)[256] = malloc(fh->sprite_count * sizeof(u32[256]));
    u32 palette[256] = {0};
    bool have_palette = false;
    u32 valid_count = 0;

    u32 header_offset = fh->first_sprite_offset;
    for (u32 i = 0; i < fh->sprite_count; i++)
    {
        Sff_V1_Sprite_Header sh;
        if (!parse_v1_sprite_header(data, file_size, header_offset, &sh))
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
                memcpy(sprite_palettes[valid_count], sprite_palettes[sh.link_index], sizeof(u32[256]));
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
        u32 block_end = (sh.next_header_offset > header_offset)
            ? sh.next_header_offset
            : pcx_offset + sh.data_size;

        bool palette_same = have_palette && (i > 0 && sh.shared_palette != 0);

        if (!palette_same)
        {
            i64 pal_marker = find_palette_marker(data, file_size, pcx_data_start, block_end);
            if (pal_marker >= 0 && (usize)(pal_marker + 769) <= file_size)
            {
                read_v1_palette(data, pal_marker, palette);
                have_palette = true;
            }
        }

        usize rle_size;
        if (palette_same)
        {
            rle_size = (usize)(block_end - pcx_data_start);
        }
        else
        {
            i64 pal_marker = find_palette_marker(data, file_size, pcx_data_start, block_end);
            rle_size = (pal_marker >= 0)
                ? (usize)(pal_marker - pcx_data_start)
                : (usize)(block_end - pcx_data_start);
        }

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
        memcpy(sprite_palettes[valid_count], palette, sizeof(u32[256]));
        valid_count++;
        header_offset = sh.next_header_offset;
    }

    if (valid_count == 0)
    {
        free(decoded);
        free(sprite_palettes);
        return false;
    }

    Pack_Pos* positions = malloc(valid_count * sizeof(Pack_Pos));
    pack_atlas(decoded, valid_count, &sff->atlas_width, &sff->atlas_height, positions);

    mel_sprite_sheet_init(&sff->sheet, alloc);
    f32 inv_w = 1.0f / (f32)sff->atlas_width;
    f32 inv_h = 1.0f / (f32)sff->atlas_height;
    for (u32 i = 0; i < valid_count; i++)
    {
        mel_sprite_sheet_push_frame(&sff->sheet,
            mel_rect((f32)positions[i].x * inv_w,
                     (f32)positions[i].y * inv_h,
                     (f32)decoded[i].width * inv_w,
                     (f32)decoded[i].height * inv_h));
    }

    usize atlas_size = (usize)sff->atlas_width * (usize)sff->atlas_height * 4;
    sff->atlas_pixels = mel_calloc(alloc, atlas_size);

    for (u32 i = 0; i < valid_count; i++)
    {
        blit_indexed_to_atlas(sff->atlas_pixels, sff->atlas_width,
                      decoded[i].pixels, decoded[i].width, decoded[i].height,
                      positions[i].x, positions[i].y,
                      sprite_palettes[i]);
    }

    free(positions);
    free(sprite_palettes);

    sff->entries = mel_calloc(alloc, valid_count * sizeof(Mugen_Sff_Entry));
    sff->entry_count = valid_count;
    for (u32 i = 0; i < valid_count; i++)
    {
        sff->entries[i] = (Mugen_Sff_Entry){
            .group = decoded[i].group,
            .number = decoded[i].number,
            .frame_index = i,
            .offset_x = decoded[i].offset_x,
            .offset_y = decoded[i].offset_y,
            .width = decoded[i].width,
            .height = decoded[i].height,
        };
    }

    for (u32 i = 0; i < valid_count; i++)
        free(decoded[i].pixels);
    free(decoded);

    return true;
}

#define MAX_PALETTES 256

typedef struct {
    u32 colors[256];
    bool loaded;
} Sff_Palette;

static bool load_v2(Mugen_Sff* sff, const u8* data, usize file_size,
                     Sff_File_Header* fh, const Mel_Alloc* alloc)
{
    Sff_Palette palettes[MAX_PALETTES] = {0};

    for (u32 i = 0; i < fh->palette_count && i < MAX_PALETTES; i++)
    {
        u32 pal_offset = fh->first_palette_offset + i * 16;
        if (pal_offset + 16 > file_size) break;

        const u8* p = data + pal_offset;
        u16 pal_link = read_u16(p + 6);
        u32 pal_data_offset = read_u32(p + 8);
        u32 pal_data_length = read_u32(p + 12);

        if (pal_data_length == 0)
        {
            if (pal_link < i && palettes[pal_link].loaded)
                memcpy(palettes[i].colors, palettes[pal_link].colors, sizeof(palettes[i].colors));
            palettes[i].loaded = true;
            continue;
        }

        u32 abs_pal_offset = fh->lofs + pal_data_offset;
        read_v2_palette(data, file_size, abs_pal_offset, pal_data_length, palettes[i].colors);
        palettes[i].loaded = true;
    }

    Decoded_Sprite* decoded = malloc(fh->sprite_count * sizeof(Decoded_Sprite));
    u32 valid_count = 0;

    bool* is_rgba = malloc(fh->sprite_count * sizeof(bool));
    memset(is_rgba, 0, fh->sprite_count * sizeof(bool));

    u16* pal_indices = malloc(fh->sprite_count * sizeof(u16));

    for (u32 i = 0; i < fh->sprite_count; i++)
    {
        u32 hdr_offset = fh->first_sprite_offset + i * 28;
        Sff_V2_Sprite_Header sh;
        if (!parse_v2_sprite_header(data, file_size, hdr_offset, &sh))
            break;

        if (sh.width == 0 || sh.height == 0)
        {
            decoded[valid_count] = (Decoded_Sprite){
                .pixels = nullptr,
                .width = 1,
                .height = 1,
                .offset_x = 0,
                .offset_y = 0,
                .group = sh.group,
                .number = sh.number,
            };
            decoded[valid_count].pixels = calloc(1, 1);
            pal_indices[valid_count] = 0;
            valid_count++;
            continue;
        }

        if (sh.data_length == 0)
        {
            if (sh.link_index < valid_count && decoded[sh.link_index].pixels)
            {
                Decoded_Sprite* src_spr = &decoded[sh.link_index];
                usize px_size;
                if (is_rgba[sh.link_index])
                    px_size = (usize)src_spr->width * (usize)src_spr->height * 4;
                else
                    px_size = (usize)src_spr->width * (usize)src_spr->height;

                decoded[valid_count] = (Decoded_Sprite){
                    .pixels = malloc(px_size),
                    .width = src_spr->width,
                    .height = src_spr->height,
                    .offset_x = sh.offset_x,
                    .offset_y = sh.offset_y,
                    .group = sh.group,
                    .number = sh.number,
                };
                memcpy(decoded[valid_count].pixels, src_spr->pixels, px_size);
                is_rgba[valid_count] = is_rgba[sh.link_index];
                pal_indices[valid_count] = pal_indices[sh.link_index];
            }
            else
            {
                decoded[valid_count] = (Decoded_Sprite){
                    .pixels = calloc(1, 1),
                    .width = 1,
                    .height = 1,
                    .offset_x = 0,
                    .offset_y = 0,
                    .group = sh.group,
                    .number = sh.number,
                };
                pal_indices[valid_count] = 0;
            }
            valid_count++;
            continue;
        }

        u32 abs_offset;
        if (sh.flags & 1)
            abs_offset = fh->tofs + sh.data_offset;
        else
            abs_offset = fh->lofs + sh.data_offset;

        u32 pixel_count = (u32)sh.width * (u32)sh.height;
        u8* pixels = nullptr;
        bool sprite_is_rgba = false;

        if (sh.format == 0)
        {
            if (sh.coldepth == 8)
            {
                if (abs_offset + sh.data_length <= file_size && sh.data_length >= pixel_count)
                {
                    pixels = malloc(pixel_count);
                    memcpy(pixels, data + abs_offset, pixel_count);
                }
            }
            else if (sh.coldepth == 24 || sh.coldepth == 32)
            {
                sprite_is_rgba = true;
                pixels = malloc(pixel_count * 4);
                const u8* raw = data + abs_offset;
                u32 bpp = sh.coldepth / 8;
                for (u32 px = 0; px < pixel_count && abs_offset + px * bpp + bpp <= file_size; px++)
                {
                    u8 b = raw[px * bpp + 0];
                    u8 g = raw[px * bpp + 1];
                    u8 r = raw[px * bpp + 2];
                    u8 a = (bpp == 4) ? raw[px * bpp + 3] : 255;
                    pixels[px * 4 + 0] = r;
                    pixels[px * 4 + 1] = g;
                    pixels[px * 4 + 2] = b;
                    pixels[px * 4 + 3] = a;
                }
            }
        }
        else if (sh.format >= 2 && sh.format <= 4)
        {
            if (abs_offset + 4 <= file_size)
            {
                u32 comp_size = sh.data_length > 4 ? sh.data_length - 4 : 0;
                const u8* comp_data = data + abs_offset + 4;
                if (abs_offset + 4 + comp_size <= file_size)
                {
                    if (sh.format == 2)
                        pixels = rle8_decode(comp_data, comp_size, pixel_count);
                    else if (sh.format == 3)
                        pixels = rle5_decode(comp_data, comp_size, pixel_count);
                    else if (sh.format == 4)
                        pixels = lz5_decode(comp_data, comp_size, pixel_count);
                }
            }
        }

        if (!pixels)
        {
            pixels = calloc(pixel_count, 1);
        }

        decoded[valid_count] = (Decoded_Sprite){
            .pixels = pixels,
            .width = sh.width,
            .height = sh.height,
            .offset_x = sh.offset_x,
            .offset_y = sh.offset_y,
            .group = sh.group,
            .number = sh.number,
        };
        is_rgba[valid_count] = sprite_is_rgba;
        pal_indices[valid_count] = sh.palette_index;
        valid_count++;
    }

    if (valid_count == 0)
    {
        free(decoded);
        free(is_rgba);
        free(pal_indices);
        return false;
    }

    Pack_Pos* positions = malloc(valid_count * sizeof(Pack_Pos));
    pack_atlas(decoded, valid_count, &sff->atlas_width, &sff->atlas_height, positions);

    mel_sprite_sheet_init(&sff->sheet, alloc);
    f32 inv_w = 1.0f / (f32)sff->atlas_width;
    f32 inv_h = 1.0f / (f32)sff->atlas_height;
    for (u32 i = 0; i < valid_count; i++)
    {
        mel_sprite_sheet_push_frame(&sff->sheet,
            mel_rect((f32)positions[i].x * inv_w,
                     (f32)positions[i].y * inv_h,
                     (f32)decoded[i].width * inv_w,
                     (f32)decoded[i].height * inv_h));
    }

    usize atlas_size = (usize)sff->atlas_width * (usize)sff->atlas_height * 4;
    sff->atlas_pixels = mel_calloc(alloc, atlas_size);

    for (u32 i = 0; i < valid_count; i++)
    {
        if (is_rgba[i])
        {
            blit_rgba_to_atlas(sff->atlas_pixels, sff->atlas_width,
                               decoded[i].pixels, decoded[i].width, decoded[i].height,
                               positions[i].x, positions[i].y);
        }
        else
        {
            u16 pal_idx = pal_indices[i];
            u32* pal = (pal_idx < MAX_PALETTES && palettes[pal_idx].loaded)
                ? palettes[pal_idx].colors
                : palettes[0].colors;
            blit_indexed_to_atlas(sff->atlas_pixels, sff->atlas_width,
                                  decoded[i].pixels, decoded[i].width, decoded[i].height,
                                  positions[i].x, positions[i].y,
                                  pal);
        }
    }

    free(positions);
    free(is_rgba);
    free(pal_indices);

    sff->entries = mel_calloc(alloc, valid_count * sizeof(Mugen_Sff_Entry));
    sff->entry_count = valid_count;
    for (u32 i = 0; i < valid_count; i++)
    {
        sff->entries[i] = (Mugen_Sff_Entry){
            .group = decoded[i].group,
            .number = decoded[i].number,
            .frame_index = i,
            .offset_x = decoded[i].offset_x,
            .offset_y = decoded[i].offset_y,
            .width = decoded[i].width,
            .height = decoded[i].height,
        };
    }

    for (u32 i = 0; i < valid_count; i++)
        free(decoded[i].pixels);
    free(decoded);

    return true;
}

bool mugen_sff_load(Mugen_Sff* sff, str8 path, const Mel_Alloc* alloc)
{
    assert(sff);
    assert(alloc);
    *sff = (Mugen_Sff){0};

    i64 fsize = 0;
    u8* data = mel_vfs_read_file(path, &fsize, alloc);
    if (!data)
    {
        SDL_Log("SFF: failed to read '%.*s'", (int)path.len, path.data);
        return false;
    }

    Sff_File_Header fh;
    if (!parse_file_header(data, (usize)fsize, &fh))
    {
        SDL_Log("SFF: invalid file header '%.*s'", (int)path.len, path.data);
        mel_dealloc(alloc, data);
        return false;
    }

    bool ok = false;
    if (fh.version_hi == 1)
        ok = load_v1(sff, data, (usize)fsize, &fh, alloc);
    else if (fh.version_hi == 2)
        ok = load_v2(sff, data, (usize)fsize, &fh, alloc);
    else
        SDL_Log("SFF: unsupported version %u '%.*s'", fh.version_hi, (int)path.len, path.data);

    mel_dealloc(alloc, data);
    return ok;
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

    mel_sprite_sheet_destroy(&sff->sheet);

    *sff = (Mugen_Sff){0};
}
