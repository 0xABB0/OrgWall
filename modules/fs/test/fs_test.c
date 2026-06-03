#include <fs/fs.h>
#include <fs/dir.h>
#include <fs/paths.h>

#include <reactor/reactor.h>
#include <future/future.h>
#include <executor/executor.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.list/list.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <test/test.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

MEL_TEST(fs, glob_matcher_star_and_question)
{
    MEL_EXPECT(mel_fs_glob_match(S8("*.txt"), S8("notes.txt"), false));
    MEL_EXPECT(!mel_fs_glob_match(S8("*.txt"), S8("notes.md"), false));
    MEL_EXPECT(mel_fs_glob_match(S8("a?c"), S8("abc"), false));
    MEL_EXPECT(!mel_fs_glob_match(S8("a?c"), S8("ac"), false));
    MEL_EXPECT(mel_fs_glob_match(S8("*"), S8("anything"), false));
    MEL_EXPECT(mel_fs_glob_match(S8("READ*.MD"), S8("readme.md"), true));
    MEL_EXPECT(!mel_fs_glob_match(S8("READ*.MD"), S8("readme.md"), false));
    MEL_EXPECT(mel_fs_glob_match(S8("a*b*c"), S8("axxbyyc"), false));
}

MEL_TEST(fs, op_null_is_invalid)
{
    Mel_Fs_Op op = MEL_FS_OP_NULL;
    MEL_EXPECT(!mel_fs_op_valid(op));
    Mel_Fs_Op live = { .index = 3, .generation = 1 };
    MEL_EXPECT(mel_fs_op_valid(live));
}

MEL_TEST(fs, status_predicates)
{
    MEL_EXPECT(mel_fs_failed(MEL_FS_ERROR | MEL_FS_NOT_FOUND));
    MEL_EXPECT(mel_fs_not_found(MEL_FS_ERROR | MEL_FS_NOT_FOUND));
    MEL_EXPECT(!mel_fs_failed(MEL_FS_OK));
    MEL_EXPECT(mel_fs_cancelled(MEL_FS_ERROR | MEL_FS_CANCELLED));
}

MEL_TEST(fs, cwd_is_absolute)
{
    Mel_Fs_Path_Result r = mel_fs_cwd(mel_alloc_heap());
    MEL_REQUIRE(!mel_fs_failed(r.status));
    MEL_REQUIRE(r.value.len > 0);
    MEL_EXPECT_EQ(r.value.data[0], (u8)'/');
    mel_dealloc(mel_alloc_heap(), r.value.data);
}

MEL_TEST(fs, folder_out_of_range_is_loud_unavailable)
{
    Mel_Fs_Path_Result r = mel_fs_folder(MEL_FS_FOLDER_COUNT, mel_alloc_heap());
    MEL_EXPECT(mel_fs_failed(r.status));
    MEL_EXPECT((r.status & MEL_FS_UNAVAILABLE) != 0u);
    if (r.value.data)
        mel_dealloc(mel_alloc_heap(), r.value.data);
}

typedef struct
{
    Mel_Reactor*  reactor;
    Mel_Fs*       fs;
    Mel_Executor* exec;
    int           turn;
    int           step;

    str8 root;
    str8 file_a;
    str8 file_b;

    Mel_Task    cont;
    Mel_Future* pending;

    bool wrote;
    bool statted;
    bool read_back;
    bool enumerated;
    bool globbed;
    bool removed;
    bool all_ok;

    Mel_Fs_Stat   seen_stat;
    usize         read_len;
    char          read_buf[64];
    u32           enum_count;
    u32           glob_count;
    bool          loop_thread_cont;
    Mel_Thread_Id loop_tid;
    bool          cont_on_loop;
} Lifecycle;

static void cont_run(Mel_Task* self);

static void arm(Lifecycle* L, Mel_Future* f)
{
    L->pending = f;
    mel_task_init(&L->cont, cont_run);
    mel_future_then(f, &L->cont, L->exec);
}

static void advance(Lifecycle* L)
{
    const Mel_Alloc* a = mel_alloc_heap();
    switch (L->step)
    {
    case 0:
        arm(L, mel_fs_mkdir(L->fs, L->root));
        break;
    case 1:
    {
        const char* payload = "melody-fs-payload";
        arm(L, mel_fs_write_file(L->fs, L->file_a, .data = (const u8*)payload, .len = strlen(payload), .create_parents = true));
        break;
    }
    case 2:
        arm(L, mel_fs_stat(L->fs, L->file_a));
        break;
    case 3:
        arm(L, mel_fs_read_file(L->fs, L->file_a));
        break;
    case 4:
        arm(L, mel_fs_enumerate(L->fs, L->root));
        break;
    case 5:
        arm(L, mel_fs_glob(L->fs, L->root, S8("*.dat")));
        break;
    case 6:
        arm(L, mel_fs_rename(L->fs, L->file_a, L->file_b));
        break;
    case 7:
        arm(L, mel_fs_remove(L->fs, L->root, .recursive = true));
        break;
    default:
        L->all_ok = true;
        mel_fs_destroy(L->fs);
        L->fs = NULL;
        mel_reactor_quit(L->reactor);
        (void)a;
        break;
    }
}

static void cont_run(Mel_Task* self)
{
    Lifecycle*  L = mel_container_of(self, Lifecycle, cont);
    Mel_Future* f = L->pending;
    L->cont_on_loop = mel_thread_id_equal(mel_thread_current_id(), L->loop_tid);

    switch (L->step)
    {
    case 0:
    {
        const Mel_Fs_Void_Result* r = mel_fs_future_void(f);
        if (!mel_fs_failed(r->status))
            L->step = 1;
        break;
    }
    case 1:
    {
        const Mel_Fs_Void_Result* r = mel_fs_future_void(f);
        L->wrote = !mel_fs_failed(r->status);
        L->step = 2;
        break;
    }
    case 2:
    {
        const Mel_Fs_Stat_Result* r = mel_fs_future_stat(f);
        L->seen_stat = r->value;
        L->statted = !mel_fs_failed(r->status) && r->value.exists && (r->value.kind & MEL_FS_KIND_FILE);
        L->step = 3;
        break;
    }
    case 3:
    {
        const Mel_Fs_Bytes_Result* r = mel_fs_future_bytes(f);
        if (!mel_fs_failed(r->status))
        {
            L->read_len = r->len;
            if (r->len < sizeof L->read_buf)
                memcpy(L->read_buf, r->data, r->len);
            L->read_back = r->len == strlen("melody-fs-payload") && memcmp(r->data, "melody-fs-payload", r->len) == 0;
        }
        L->step = 4;
        break;
    }
    case 4:
    {
        const Mel_Fs_Dir_Result* r = mel_fs_future_dir(f);
        L->enum_count = r->count;
        L->enumerated = !mel_fs_failed(r->status) && r->count >= 1;
        L->step = 5;
        break;
    }
    case 5:
    {
        const Mel_Fs_Dir_Result* r = mel_fs_future_dir(f);
        L->glob_count = r->count;
        L->globbed = !mel_fs_failed(r->status) && r->count == 1;
        L->step = 6;
        break;
    }
    case 6:
    {
        const Mel_Fs_Void_Result* r = mel_fs_future_void(f);
        if (!mel_fs_failed(r->status))
            L->step = 7;
        else
            L->step = 7;
        break;
    }
    case 7:
    {
        const Mel_Fs_Void_Result* r = mel_fs_future_void(f);
        L->removed = !mel_fs_failed(r->status);
        L->step = 8;
        break;
    }
    default:
        break;
    }

    mel_fs_future_release(f);
    L->pending = NULL;
    advance(L);
}

static bool lifecycle_idle(void* user)
{
    Lifecycle* L = (Lifecycle*)user;
    L->turn++;
    if (L->turn == 2 && L->pending == NULL && !L->all_ok)
        advance(L);
    if (L->turn > 100000)
    {
        if (L->fs)
        {
            mel_fs_destroy(L->fs);
            L->fs = NULL;
        }
        mel_reactor_quit(L->reactor);
    }
    return true;
}

static bool lifecycle_init(Mel_Reactor* r, void* user)
{
    Lifecycle* L = (Lifecycle*)user;
    L->reactor = r;
    L->loop_tid = mel_thread_current_id();
    L->fs = mel_fs_create(.reactor = r);
    L->exec = mel_fs_executor(L->fs);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(lifecycle_idle, L);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(fs, async_lifecycle_mkdir_write_stat_read_enumerate_glob_rename_remove)
{
    Lifecycle L = { 0 };
    char      tmpl[] = "/tmp/melfs-XXXXXX";
    char*     dir = mkdtemp(tmpl);
    MEL_REQUIRE_NOT_NULL(dir);

    char root[256];
    char file_a[256];
    char file_b[256];
    snprintf(root, sizeof root, "%s/work", dir);
    snprintf(file_a, sizeof file_a, "%s/work/data.dat", dir);
    snprintf(file_b, sizeof file_b, "%s/work/data.bak", dir);
    L.root = str8_from_cstr(root);
    L.file_a = str8_from_cstr(file_a);
    L.file_b = str8_from_cstr(file_b);

    mel_reactor_spawn(MEL_REACTOR_THREADED, lifecycle_init, &L);

    MEL_EXPECT(L.all_ok);
    MEL_EXPECT(L.wrote);
    MEL_EXPECT(L.statted);
    MEL_EXPECT_EQ((i64)L.seen_stat.size_bytes, (i64)strlen("melody-fs-payload"));
    MEL_EXPECT(L.read_back);
    MEL_EXPECT(L.enumerated);
    MEL_EXPECT(L.globbed);
    MEL_EXPECT_EQ(L.glob_count, 1u);
    MEL_EXPECT(L.removed);
    MEL_EXPECT(L.cont_on_loop);

    rmdir(dir);
}

typedef struct
{
    Mel_Reactor* reactor;
    Mel_Fs*      fs;
    int          turn;
    Mel_Task     cont;
    Mel_Future*  pending;
    bool         done;
    bool         not_found_ok;
} Missing;

static void missing_cont(Mel_Task* self)
{
    Missing*                  m = mel_container_of(self, Missing, cont);
    const Mel_Fs_Stat_Result* r = mel_fs_future_stat(m->pending);
    m->not_found_ok = !mel_fs_failed(r->status) && !r->value.exists;
    mel_fs_future_release(m->pending);
    m->pending = NULL;
    m->done = true;
    mel_fs_destroy(m->fs);
    m->fs = NULL;
    mel_reactor_quit(m->reactor);
}

static bool missing_idle(void* user)
{
    Missing* m = (Missing*)user;
    m->turn++;
    if (m->turn == 2 && m->pending == NULL && !m->done)
    {
        m->pending = mel_fs_stat(m->fs, S8("/tmp/melody-this-does-not-exist-xyz"));
        mel_task_init(&m->cont, missing_cont);
        mel_future_then(m->pending, &m->cont, mel_fs_executor(m->fs));
    }
    if (m->turn > 100000)
        mel_reactor_quit(m->reactor);
    return true;
}

static bool missing_init(Mel_Reactor* r, void* user)
{
    Missing* m = (Missing*)user;
    m->reactor = r;
    m->fs = mel_fs_create(.reactor = r);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(missing_idle, m);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(fs, stat_missing_path_is_ok_with_exists_false)
{
    Missing m = { 0 };
    mel_reactor_spawn(MEL_REACTOR_THREADED, missing_init, &m);
    MEL_EXPECT(m.done);
    MEL_EXPECT(m.not_found_ok);
}
