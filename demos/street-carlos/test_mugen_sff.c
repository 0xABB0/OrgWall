#include "test.harness.h"
#include "mugen_sff.h"
#include "math.geo.rect.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include <SDL3/SDL.h>

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static bool s_vfs_ready = false;

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
