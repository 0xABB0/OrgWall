#include "test.harness.h"
#include "mugen.roster.h"
#include "mugen.char.h"
#include "string.str8.h"
#include "allocator.heap.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"
// #include "vfs.backend.os.h"
// ASYNC_V2: removed, needs migration
// #include "async.io.h"
// #include "async.task.h"

// ASYNC_V2: VFS removed — all roster tests depend on VFS+Task, currently non-functional

MEL_TEST(roster_init_shutdown, .tags = "mugen")
{
    Mugen_Roster r;
    mugen_roster_init(&r,
        .vfs = NULL,
        .task_ctx = NULL,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(mugen_roster_count(&r), 0u);

    mugen_roster_shutdown(&r);
}
