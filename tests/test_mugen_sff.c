#include "test.harness.h"
#include "mugen.sff.h"
#include "math.geo.rect.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static bool s_vfs_ready = false;

static void write_u16_le(u8* dst, u16 value)
{
    dst[0] = (u8)(value & 0xFF);
    dst[1] = (u8)((value >> 8) & 0xFF);
}

static void write_u32_le(u8* dst, u32 value)
{
    dst[0] = (u8)(value & 0xFF);
    dst[1] = (u8)((value >> 8) & 0xFF);
    dst[2] = (u8)((value >> 16) & 0xFF);
    dst[3] = (u8)((value >> 24) & 0xFF);
}

static void build_test_v1_palette_sff(u8* bytes, usize size)
{
    const u32 file_header_size = 0x200;
    const u32 sprite_blob_size = 32 + 128 + 1 + 769;
    const u32 sprite1_offset = file_header_size;
    const u32 sprite2_offset = sprite1_offset + sprite_blob_size;

    memset(bytes, 0, size);

    memcpy(bytes, "ElecbyteSpr\0", 12);
    bytes[13] = 1;
    bytes[15] = 1;
    write_u32_le(bytes + 20, 2);
    write_u32_le(bytes + 24, sprite1_offset);

    u8* spr1 = bytes + sprite1_offset;
    write_u32_le(spr1 + 0, sprite2_offset);
    write_u32_le(spr1 + 4, sprite_blob_size - 32);
    write_u16_le(spr1 + 12, 1);
    write_u16_le(spr1 + 14, 0);
    write_u16_le(spr1 + 16, 0);
    spr1[18] = 0;

    u8* pcx1 = spr1 + 32;
    pcx1[0] = 0x0A;
    pcx1[1] = 5;
    pcx1[2] = 0;
    pcx1[3] = 8;
    write_u16_le(pcx1 + 4, 0);
    write_u16_le(pcx1 + 6, 0);
    write_u16_le(pcx1 + 8, 0);
    write_u16_le(pcx1 + 10, 0);
    write_u16_le(pcx1 + 66, 1);
    pcx1[128] = 1;
    pcx1[129] = 0x0C;
    pcx1[133] = 255;

    u8* spr2 = bytes + sprite2_offset;
    write_u32_le(spr2 + 0, 0);
    write_u32_le(spr2 + 4, sprite_blob_size - 32);
    write_u16_le(spr2 + 12, 2);
    write_u16_le(spr2 + 14, 0);
    write_u16_le(spr2 + 16, 0);
    spr2[18] = 0;

    u8* pcx2 = spr2 + 32;
    pcx2[0] = 0x0A;
    pcx2[1] = 5;
    pcx2[2] = 0;
    pcx2[3] = 8;
    write_u16_le(pcx2 + 4, 0);
    write_u16_le(pcx2 + 6, 0);
    write_u16_le(pcx2 + 8, 0);
    write_u16_le(pcx2 + 10, 0);
    write_u16_le(pcx2 + 66, 1);
    pcx2[128] = 1;
    pcx2[129] = 0x0C;
    pcx2[134] = 255;
}

static void sample_frame_rgba(Mugen_Sff* sff, u32 frame_idx, u8 rgba[4])
{
    Mel_Rect uv = mel_sprite_sheet_frame(&sff->sheet, frame_idx);
    u32 px_x = (u32)(uv.x * (f32)sff->atlas_width);
    u32 px_y = (u32)(uv.y * (f32)sff->atlas_height);
    u32 idx = (px_y * sff->atlas_width + px_x) * 4;
    rgba[0] = sff->atlas_pixels[idx + 0];
    rgba[1] = sff->atlas_pixels[idx + 1];
    rgba[2] = sff->atlas_pixels[idx + 2];
    rgba[3] = sff->atlas_pixels[idx + 3];
}

static void ensure_vfs(void)
{
    if (s_vfs_ready) return;

    mel_io_init(&s_io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_io });

    Mel_Vfs_Backend* os = mel_vfs_backend_os_create(mel_alloc_heap(), S8("demos/street-carlos"));
    mel_vfs_mount(&s_vfs, S8("/"), os, 0, false);
    s_vfs_ready = true;
}

MEL_TEST(sff_load_poison, .tags = "mugen")
{
    ensure_vfs();

    Mugen_Sff sff;
    bool ok = mugen_sff_load(&sff, &s_vfs, S8("/chars/poi-son/poi-son.sff"), mel_alloc_heap());
    MEL_ASSERT(ok);
    MEL_ASSERT_GT(sff.entry_count, (u32)0);
    MEL_ASSERT_GT(sff.atlas_width, (u32)0);
    MEL_ASSERT_GT(sff.atlas_height, (u32)0);
    MEL_ASSERT_NOT_NULL(sff.atlas_pixels);
    MEL_ASSERT_EQ(sff.sheet.frame_count, sff.entry_count);

    SDL_Log("  Sprites: %u, Atlas: %ux%u", sff.entry_count, sff.atlas_width, sff.atlas_height);

    u32 frame_0_0 = mugen_sff_find_frame(&sff, 0, 0);
    Mel_Rect uv = mel_sprite_sheet_frame(&sff.sheet, frame_0_0);
    MEL_ASSERT(uv.w > 0.0f);
    MEL_ASSERT(uv.h > 0.0f);

    Mugen_Sff_Entry* e = &sff.entries[frame_0_0];
    MEL_ASSERT_GT(e->width, (u16)0);
    MEL_ASSERT_GT(e->height, (u16)0);

    SDL_Log("  Sprite [0,0]: %ux%u uv(%.3f,%.3f,%.3f,%.3f) offset(%d,%d)",
            e->width, e->height, uv.x, uv.y, uv.w, uv.h, e->offset_x, e->offset_y);

    Mugen_Sff_Entry* se = &sff.entries[frame_0_0];
    Mel_Rect suv = mel_sprite_sheet_frame(&sff.sheet, frame_0_0);
    u32 px_x = (u32)(suv.x * (f32)sff.atlas_width);
    u32 px_y = (u32)(suv.y * (f32)sff.atlas_height);
    u32 last_opaque_row = 0;
    for (u32 row = 0; row < se->height; row++)
    {
        for (u32 col = 0; col < se->width; col++)
        {
            u32 idx = ((px_y + row) * sff.atlas_width + (px_x + col)) * 4;
            if (sff.atlas_pixels[idx + 3] > 0) { last_opaque_row = row; break; }
        }
    }
    MEL_ASSERT_GT(last_opaque_row, se->height / 2);

    mugen_sff_shutdown(&sff, mel_alloc_heap());
}

MEL_TEST(sff_v1_uses_header_shared_palette_flag, .tags = "mugen")
{
    enum { TEST_SFF_SIZE = 0x944 };

    char temp_dir[] = "/tmp/melody-sff-v1-XXXXXX";
    char* dir = mkdtemp(temp_dir);
    MEL_ASSERT_NOT_NULL(dir);

    char sff_path[512];
    snprintf(sff_path, sizeof(sff_path), "%s/test.sff", dir);

    u8 bytes[TEST_SFF_SIZE];
    build_test_v1_palette_sff(bytes, sizeof(bytes));

    FILE* file = fopen(sff_path, "wb");
    MEL_ASSERT_NOT_NULL(file);
    MEL_ASSERT_EQ(fwrite(bytes, 1, sizeof(bytes), file), sizeof(bytes));
    fclose(file);

    Mel_Io io;
    mel_io_init(&io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });

    Mel_Vfs vfs;
    mel_vfs_init(&vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &io });
    Mel_Vfs_Backend* os = mel_vfs_backend_os_create(mel_alloc_heap(), str8_from_cstr(dir));
    mel_vfs_mount(&vfs, S8("/"), os, 0, false);

    Mugen_Sff sff;
    bool ok = mugen_sff_load(&sff, &vfs, S8("/test.sff"), mel_alloc_heap());
    MEL_ASSERT(ok);

    u8 first_rgba[4];
    u8 second_rgba[4];
    sample_frame_rgba(&sff, mugen_sff_find_frame(&sff, 1, 0), first_rgba);
    sample_frame_rgba(&sff, mugen_sff_find_frame(&sff, 2, 0), second_rgba);

    MEL_ASSERT_EQ(first_rgba[0], (u8)255);
    MEL_ASSERT_EQ(first_rgba[1], (u8)0);
    MEL_ASSERT_EQ(first_rgba[2], (u8)0);
    MEL_ASSERT_EQ(first_rgba[3], (u8)255);

    MEL_ASSERT_EQ(second_rgba[0], (u8)0);
    MEL_ASSERT_EQ(second_rgba[1], (u8)255);
    MEL_ASSERT_EQ(second_rgba[2], (u8)0);
    MEL_ASSERT_EQ(second_rgba[3], (u8)255);

    mugen_sff_shutdown(&sff, mel_alloc_heap());
    mel_vfs_shutdown(&vfs);
    mel_io_shutdown(&io);

    unlink(sff_path);
    rmdir(dir);
}
