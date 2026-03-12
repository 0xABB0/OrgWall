#include "test.harness.h"
#include "mugen.roster.h"
#include "mugen.char.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "async.task.h"

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static Mel_Task_Ctx* s_task_ctx;
static bool s_inited = false;

static void ensure_inited(void)
{
    if (s_inited) return;

    mel_io_init(&s_io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_io });
    Mel_Vfs_Backend* os_be = mel_vfs_backend_os_create(mel_alloc_heap(), S8("demos/street-carlos"));
    mel_vfs_mount(&s_vfs, S8("/"), os_be, 0, false);

    Mel_Task_Ctx_Desc task_desc = {
        .alloc = mel_alloc_heap(),
        .io    = &s_io,
        .vfs   = &s_vfs,
    };
    s_task_ctx = mel_task_ctx_create(&task_desc);

    s_inited = true;
}

MEL_TEST(roster_init_shutdown, .tags = "mugen")
{
    ensure_inited();

    Mugen_Roster r;
    mugen_roster_init(&r,
        .vfs = &s_vfs,
        .task_ctx = s_task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(mugen_roster_count(&r), 0u);

    mugen_roster_shutdown(&r);
}

MEL_TEST(roster_load_finds_characters, .tags = "mugen")
{
    ensure_inited();

    Mugen_Roster r;
    mugen_roster_init(&r,
        .vfs = &s_vfs,
        .task_ctx = s_task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    Mel_Task_Handle h = mugen_roster_load(&r, .folder_path = S8("/chars/"));

    while (!mel_task_is_done(s_task_ctx, h))
        mel_task_tick(s_task_ctx);

    mel_task_release(s_task_ctx, h);

    MEL_ASSERT(mugen_roster_count(&r) >= 2);

    mugen_roster_shutdown(&r);
}

MEL_TEST(roster_find_by_name, .tags = "mugen")
{
    ensure_inited();

    Mugen_Roster r;
    mugen_roster_init(&r,
        .vfs = &s_vfs,
        .task_ctx = s_task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    Mel_Task_Handle h = mugen_roster_load(&r, .folder_path = S8("/chars/"));

    while (!mel_task_is_done(s_task_ctx, h))
        mel_task_tick(s_task_ctx);

    mel_task_release(s_task_ctx, h);

    Mugen_Char* kfm = mugen_roster_find(&r, S8("kfm"));
    MEL_ASSERT_NOT_NULL(kfm);
    MEL_ASSERT(kfm->loaded);

    Mugen_Char* poison = mugen_roster_find(&r, S8("poi-son"));
    MEL_ASSERT_NOT_NULL(poison);
    MEL_ASSERT(poison->loaded);

    Mugen_Char* missing = mugen_roster_find(&r, S8("nonexistent"));
    MEL_ASSERT_NULL(missing);

    mugen_roster_shutdown(&r);
}

MEL_TEST(roster_index_access, .tags = "mugen")
{
    ensure_inited();

    Mugen_Roster r;
    mugen_roster_init(&r,
        .vfs = &s_vfs,
        .task_ctx = s_task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    Mel_Task_Handle h = mugen_roster_load(&r, .folder_path = S8("/chars/"));

    while (!mel_task_is_done(s_task_ctx, h))
        mel_task_tick(s_task_ctx);

    mel_task_release(s_task_ctx, h);

    MEL_ASSERT(mugen_roster_count(&r) >= 2);
    MEL_ASSERT_NOT_NULL(mugen_roster_at(&r, 0));
    MEL_ASSERT(mugen_roster_name_at(&r, 0).len > 0);
    MEL_ASSERT_NULL(mugen_roster_at(&r, mugen_roster_count(&r)));
    MEL_ASSERT_EQ(mugen_roster_name_at(&r, mugen_roster_count(&r)).len, 0u);

    mugen_roster_shutdown(&r);
}

static bool s_complete_called;
static u32 s_complete_status;

static void test_on_complete(Mel_Task_Handle handle, u32 status, void* user)
{
    (void)handle;
    (void)user;
    s_complete_called = true;
    s_complete_status = status;
}

MEL_TEST(roster_load_on_complete_fires, .tags = "mugen")
{
    ensure_inited();

    s_complete_called = false;
    s_complete_status = 0;

    Mugen_Roster r;
    mugen_roster_init(&r,
        .vfs = &s_vfs,
        .task_ctx = s_task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    Mel_Task_Handle h = mugen_roster_load(&r,
        .folder_path = S8("/chars/"),
        .on_complete = test_on_complete);

    while (!mel_task_is_done(s_task_ctx, h))
        mel_task_tick(s_task_ctx);

    mel_task_tick(s_task_ctx);

    MEL_ASSERT(s_complete_called);
    MEL_ASSERT_EQ(s_complete_status, (u32)MEL_TASK_STATUS_DONE);

    mel_task_release(s_task_ctx, h);
    mugen_roster_shutdown(&r);
}
