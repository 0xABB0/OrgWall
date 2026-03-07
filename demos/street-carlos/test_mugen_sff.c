#include "test.harness.h"
#include "mugen_sff.h"
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
    Mel_SpriteFrame* f = mel_spritesheet_get_frame(&sff.sheet, frame_0_0);
    MEL_ASSERT_NOT_NULL(f);
    MEL_ASSERT_GT(f->width, (u32)0);
    MEL_ASSERT_GT(f->height, (u32)0);

    SDL_Log("  Sprite [0,0]: %ux%u at atlas(%u,%u) offset(%d,%d)",
            f->width, f->height, f->x, f->y, f->offset_x, f->offset_y);

    mugen_sff_shutdown(&sff, mel_alloc_heap());
}
