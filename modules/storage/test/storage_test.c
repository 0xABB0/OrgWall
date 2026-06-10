#include <storage/storage.h>
#include <storage/status.h>

#include <vat/vat.h>
#include <future/future.h>
#include <executor/executor.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <collection/list.h>
#include <test/test.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

MEL_TEST(storage, path_validation)
{
    MEL_EXPECT(mel_storage_path_valid(S8("saves/slot1.dat")));
    MEL_EXPECT(mel_storage_path_valid(S8("a/b/c.bin")));
    MEL_EXPECT(mel_storage_path_valid(S8("")));
    MEL_EXPECT(!mel_storage_path_valid(S8("/etc/passwd")));
    MEL_EXPECT(!mel_storage_path_valid(S8("../escape")));
    MEL_EXPECT(!mel_storage_path_valid(S8("a/../b")));
    MEL_EXPECT(!mel_storage_path_valid(S8("a/..")));
    MEL_EXPECT(!mel_storage_path_valid(S8("..")));
    MEL_EXPECT(!mel_storage_path_valid(S8("C:/windows")));
    MEL_EXPECT(!mel_storage_path_valid(S8("a\\b")));
    MEL_EXPECT(mel_storage_path_valid(S8("..dotfile")));
    MEL_EXPECT(mel_storage_path_valid(S8("a/b..c/d")));
}

MEL_TEST(storage, status_predicates)
{
    MEL_EXPECT(mel_storage_failed(MEL_STORAGE_ERROR | MEL_STORAGE_NOT_FOUND));
    MEL_EXPECT(mel_storage_not_found(MEL_STORAGE_ERROR | MEL_STORAGE_NOT_FOUND));
    MEL_EXPECT(!mel_storage_failed(MEL_STORAGE_OK));
    MEL_EXPECT(mel_storage_ok(MEL_STORAGE_OK));
    MEL_EXPECT(mel_storage_read_only(MEL_STORAGE_ERROR | MEL_STORAGE_READ_ONLY));
    MEL_EXPECT(mel_storage_escape(MEL_STORAGE_ERROR | MEL_STORAGE_ESCAPE));
    MEL_EXPECT(mel_storage_cancelled(MEL_STORAGE_ERROR | MEL_STORAGE_CANCELLED));
}

MEL_TEST(storage, op_handle)
{
    Mel_Storage_Op op = MEL_STORAGE_OP_NULL;
    MEL_EXPECT(!mel_storage_op_valid(op));
    Mel_Storage_Op live = { .index = 4, .generation = 2 };
    MEL_EXPECT(mel_storage_op_valid(live));
    MEL_EXPECT(mel_storage_op_equal(live, (Mel_Storage_Op){ .index = 4, .generation = 2 }));
    MEL_EXPECT(!mel_storage_op_equal(live, op));
}

typedef struct
{
    bool (*idle)(void* user);
    void* user;
} Vat_Idle;

static i64 vat_idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool vat_idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Vat_Idle* w = (Vat_Idle*)mel_vat_source_state(s);
    w->idle(w->user);
    return false;
}

static const Mel_Vat_Source_Vtbl VAT_IDLE_VT = {
    .wakeables = NULL,
    .deadline = vat_idle_deadline,
    .drain = vat_idle_drain,
    .cancel = NULL,
};

static void run_on_vat(bool (*init)(Mel_Vat*, void*), bool (*idle)(void*), void* user)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_io(a);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(a, 64);
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    Vat_Idle         w = { .idle = idle, .user = user };
    if (init(vat, user))
    {
        Mel_Vat_Source* src = mel_vat_source_open(vat, &VAT_IDLE_VT, &w);
        mel_vat_run(vat);
        mel_vat_source_close(src);
    }
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}

typedef struct
{
    Mel_Vat*      vat;
    Mel_Storage*  st;
    Mel_Executor* exec;
    str8          root;
    int           turn;
    int           step;

    Mel_Task    cont;
    Mel_Future* pending;

    bool all_ok;
    bool wrote;
    bool sized;
    bool metaed;
    bool read_back;
    bool size_mismatch_caught;
    bool enumerated;
    bool globbed;
    bool renamed;
    bool copied;
    bool spaced;
    bool removed;
    bool escape_rejected;
    bool batch_seen;

    u64 seen_size;
    u32 enum_count;
    u32 glob_count;
} Life;

static const char* PAYLOAD = "melody-storage-payload";

static void cont_run(Mel_Task* self);

static void arm(Life* L, Mel_Future* f)
{
    L->pending = f;
    mel_task_init(&L->cont, cont_run);
    mel_future_then(f, &L->cont, L->exec);
}

static void on_batch(const Mel_Storage_Entry* entries, u32 count, void* user)
{
    Life* L = (Life*)user;
    (void)entries;
    if (count > 0)
        L->batch_seen = true;
}

static void advance(Life* L)
{
    switch (L->step)
    {
    case 0:
        arm(L, mel_storage_write(L->st, S8("saves/slot1.dat"), .data = (const u8*)PAYLOAD, .len = strlen(PAYLOAD)));
        break;
    case 1:
        arm(L, mel_storage_size(L->st, S8("saves/slot1.dat")));
        break;
    case 2:
        arm(L, mel_storage_meta(L->st, S8("saves/slot1.dat")));
        break;
    case 3:
        arm(L, mel_storage_read(L->st, S8("saves/slot1.dat"), .expect = strlen(PAYLOAD)));
        break;
    case 4:
        arm(L, mel_storage_read(L->st, S8("saves/slot1.dat"), .expect = 3));
        break;
    case 5:
        arm(L, mel_storage_enumerate(L->st, S8("saves"), .on_batch = on_batch, .stream_user = L));
        break;
    case 6:
        arm(L, mel_storage_glob(L->st, S8("saves"), S8("*.dat")));
        break;
    case 7:
        arm(L, mel_storage_rename(L->st, S8("saves/slot1.dat"), S8("saves/slot1.bak")));
        break;
    case 8:
        arm(L, mel_storage_copy(L->st, S8("saves/slot1.bak"), S8("saves/slot1.copy")));
        break;
    case 9:
        arm(L, mel_storage_space(L->st));
        break;
    case 10:
        arm(L, mel_storage_read(L->st, S8("../escape.dat")));
        break;
    case 11:
        arm(L, mel_storage_remove(L->st, S8("saves"), .recursive = true));
        break;
    default:
        L->all_ok = true;
        mel_storage_destroy(L->st);
        L->st = NULL;
        mel_vat_quit(L->vat);
        break;
    }
}

static void cont_run(Mel_Task* self)
{
    Life*       L = mel_container_of(self, Life, cont);
    Mel_Future* f = L->pending;

    switch (L->step)
    {
    case 0:
    {
        const Mel_Storage_Void_Result* r = mel_storage_future_void(f);
        L->wrote = mel_storage_ok(r->status);
        L->step = 1;
        break;
    }
    case 1:
    {
        const Mel_Storage_Size_Result* r = mel_storage_future_size(f);
        L->seen_size = r->value;
        L->sized = mel_storage_ok(r->status) && r->value == strlen(PAYLOAD);
        L->step = 2;
        break;
    }
    case 2:
    {
        const Mel_Storage_Meta_Result* r = mel_storage_future_meta(f);
        L->metaed = mel_storage_ok(r->status) && r->value.exists && (r->value.kind & MEL_STORAGE_KIND_FILE) && r->value.size_bytes == strlen(PAYLOAD);
        L->step = 3;
        break;
    }
    case 3:
    {
        const Mel_Storage_Bytes* r = mel_storage_future_bytes(f);
        L->read_back = mel_storage_ok(r->status) && r->len == strlen(PAYLOAD) && memcmp(r->data, PAYLOAD, r->len) == 0;
        L->step = 4;
        break;
    }
    case 4:
    {
        const Mel_Storage_Bytes* r = mel_storage_future_bytes(f);
        L->size_mismatch_caught = mel_storage_failed(r->status) && (r->status & MEL_STORAGE_SIZE_MISMATCH);
        L->step = 5;
        break;
    }
    case 5:
    {
        const Mel_Storage_List_Result* r = mel_storage_future_list(f);
        L->enum_count = r->count;
        L->enumerated = mel_storage_ok(r->status) && r->count >= 1;
        L->step = 6;
        break;
    }
    case 6:
    {
        const Mel_Storage_List_Result* r = mel_storage_future_list(f);
        L->glob_count = r->count;
        L->globbed = mel_storage_ok(r->status) && r->count == 1;
        L->step = 7;
        break;
    }
    case 7:
    {
        const Mel_Storage_Void_Result* r = mel_storage_future_void(f);
        L->renamed = mel_storage_ok(r->status);
        L->step = 8;
        break;
    }
    case 8:
    {
        const Mel_Storage_Void_Result* r = mel_storage_future_void(f);
        L->copied = mel_storage_ok(r->status);
        L->step = 9;
        break;
    }
    case 9:
    {
        const Mel_Storage_Space_Result* r = mel_storage_future_space(f);
        L->spaced = mel_storage_ok(r->status) && r->value.total_bytes > 0;
        L->step = 10;
        break;
    }
    case 10:
    {
        const Mel_Storage_Bytes* r = mel_storage_future_bytes(f);
        L->escape_rejected = mel_storage_failed(r->status) && (r->status & MEL_STORAGE_ESCAPE);
        L->step = 11;
        break;
    }
    case 11:
    {
        const Mel_Storage_Void_Result* r = mel_storage_future_void(f);
        L->removed = mel_storage_ok(r->status);
        L->step = 12;
        break;
    }
    default:
        break;
    }

    mel_storage_future_release(f);
    L->pending = NULL;
    advance(L);
}

static bool life_idle(void* user)
{
    Life* L = (Life*)user;
    L->turn++;
    if (L->turn == 2 && L->pending == NULL && !L->all_ok)
        advance(L);
    if (L->turn > 200000)
    {
        if (L->st)
        {
            mel_storage_destroy(L->st);
            L->st = NULL;
        }
        mel_vat_quit(L->vat);
    }
    return true;
}

static bool life_init(Mel_Vat* vat, void* user)
{
    Life* L = (Life*)user;
    L->vat = vat;
    L->st = mel_storage_open_fs(.root = L->root, .vat = vat);
    if (!L->st)
        return false;
    L->exec = mel_storage_executor(L->st);
    return true;
}

MEL_TEST(storage, fs_backed_lifecycle)
{
    Life  L = { 0 };
    char  tmpl[] = "/tmp/melstorage-XXXXXX";
    char* dir = mkdtemp(tmpl);
    MEL_REQUIRE_NOT_NULL(dir);

    char root[256];
    snprintf(root, sizeof root, "%s/sandbox", dir);
    L.root = str8_from_cstr(root);

    run_on_vat(life_init, life_idle, &L);

    MEL_EXPECT(L.all_ok);
    MEL_EXPECT(L.wrote);
    MEL_EXPECT(L.sized);
    MEL_EXPECT_EQ((i64)L.seen_size, (i64)strlen(PAYLOAD));
    MEL_EXPECT(L.metaed);
    MEL_EXPECT(L.read_back);
    MEL_EXPECT(L.size_mismatch_caught);
    MEL_EXPECT(L.enumerated);
    MEL_EXPECT(L.batch_seen);
    MEL_EXPECT(L.globbed);
    MEL_EXPECT_EQ(L.glob_count, 1u);
    MEL_EXPECT(L.renamed);
    MEL_EXPECT(L.copied);
    MEL_EXPECT(L.spaced);
    MEL_EXPECT(L.escape_rejected);
    MEL_EXPECT(L.removed);

    char cleanup[300];
    snprintf(cleanup, sizeof cleanup, "rm -rf %s", dir);
    (void)system(cleanup);
}

typedef struct
{
    Mel_Vat*     vat;
    Mel_Storage* st;
    int          turn;
    Mel_Task     cont;
    Mel_Future*  pending;
    bool         done;
    bool         read_only_rejected;
    bool         not_writable;
} ReadOnly;

static void ro_cont(Mel_Task* self)
{
    ReadOnly*                      ro = mel_container_of(self, ReadOnly, cont);
    const Mel_Storage_Void_Result* r = mel_storage_future_void(ro->pending);
    ro->read_only_rejected = mel_storage_failed(r->status) && (r->status & MEL_STORAGE_READ_ONLY);
    mel_storage_future_release(ro->pending);
    ro->pending = NULL;
    ro->done = true;
    mel_storage_destroy(ro->st);
    ro->st = NULL;
    mel_vat_quit(ro->vat);
}

static bool ro_idle(void* user)
{
    ReadOnly* ro = (ReadOnly*)user;
    ro->turn++;
    if (ro->turn == 2 && ro->pending == NULL && !ro->done)
    {
        const char* body = "nope";
        ro->pending = mel_storage_write(ro->st, S8("x.dat"), .data = (const u8*)body, .len = 4);
        mel_task_init(&ro->cont, ro_cont);
        mel_future_then(ro->pending, &ro->cont, mel_storage_executor(ro->st));
    }
    if (ro->turn > 200000)
        mel_vat_quit(ro->vat);
    return true;
}

static bool ro_init(Mel_Vat* vat, void* user)
{
    ReadOnly* ro = (ReadOnly*)user;
    ro->vat = vat;
    char  tmpl[] = "/tmp/melstorage-ro-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir)
        return false;
    ro->st = mel_storage_open_fs_opt((Mel_Storage_Fs_Opt){ .root = str8_from_cstr(dir), .vat = vat, .writable = false, .create_root = false });
    if (!ro->st)
        return false;
    ro->not_writable = !mel_storage_writable(ro->st);
    return true;
}

MEL_TEST(storage, read_only_rejects_writes)
{
    ReadOnly ro = { 0 };
    run_on_vat(ro_init, ro_idle, &ro);
    MEL_EXPECT(ro.done);
    MEL_EXPECT(ro.not_writable);
    MEL_EXPECT(ro.read_only_rejected);
}

typedef struct
{
    Mel_Vat*     vat;
    Mel_Storage* st;
    int          turn;
    bool         destroyed;
    bool         survived;
} DestroyFlight;

static bool df_idle(void* user)
{
    DestroyFlight* d = (DestroyFlight*)user;
    d->turn++;
    if (d->turn == 2 && !d->destroyed)
    {
        const char* body = "destroy-during-flight";
        (void)mel_storage_write(d->st, S8("saves/slot.dat"), .data = (const u8*)body, .len = strlen(body));
        mel_storage_destroy(d->st);
        d->st = NULL;
        d->destroyed = true;
    }
    if (d->destroyed && d->turn > 12)
    {
        d->survived = true;
        mel_vat_quit(d->vat);
    }
    if (d->turn > 200000)
        mel_vat_quit(d->vat);
    return true;
}

static bool df_init(Mel_Vat* vat, void* user)
{
    DestroyFlight* d = (DestroyFlight*)user;
    d->vat = vat;
    char  tmpl[] = "/tmp/melstorage-df-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir)
        return false;
    char root[256];
    snprintf(root, sizeof root, "%s/sandbox", dir);
    d->st = mel_storage_open_fs(.root = str8_from_cstr(root), .vat = vat);
    if (!d->st)
        return false;
    return true;
}

MEL_TEST(storage, destroy_during_flight)
{
    DestroyFlight d = { 0 };
    run_on_vat(df_init, df_idle, &d);
    MEL_EXPECT(d.destroyed);
    MEL_EXPECT(d.survived);
}

typedef struct
{
    Mel_Vat*       vat;
    Mel_Storage*   st;
    int            turn;
    Mel_Task       cont;
    Mel_Future*    pending;
    Mel_Storage_Op op;
    bool           armed;
    bool           done;
} CancelFlight;

static void cf_cont(Mel_Task* self)
{
    CancelFlight* c = mel_container_of(self, CancelFlight, cont);
    mel_storage_future_release(c->pending);
    c->pending = NULL;
    c->done = true;
    mel_storage_destroy(c->st);
    c->st = NULL;
    mel_vat_quit(c->vat);
}

static bool cf_idle(void* user)
{
    CancelFlight* c = (CancelFlight*)user;
    c->turn++;
    if (c->turn == 2 && !c->armed)
    {
        const char* body = "cancel-me";
        c->pending = mel_storage_write(c->st, S8("saves/slot.dat"), .data = (const u8*)body, .len = strlen(body), .out_op = &c->op);
        mel_task_init(&c->cont, cf_cont);
        mel_future_then(c->pending, &c->cont, mel_storage_executor(c->st));
        bool ok = mel_storage_cancel(c->st, c->op);
        (void)ok;
        c->armed = true;
    }
    if (c->turn > 200000)
        mel_vat_quit(c->vat);
    return true;
}

static bool cf_init(Mel_Vat* vat, void* user)
{
    CancelFlight* c = (CancelFlight*)user;
    c->vat = vat;
    char  tmpl[] = "/tmp/melstorage-cf-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir)
        return false;
    char root[256];
    snprintf(root, sizeof root, "%s/sandbox", dir);
    c->st = mel_storage_open_fs(.root = str8_from_cstr(root), .vat = vat);
    if (!c->st)
        return false;
    return true;
}

MEL_TEST(storage, cancel_settles_future)
{
    CancelFlight c = { 0 };
    run_on_vat(cf_init, cf_idle, &c);
    MEL_EXPECT(c.done);
}
