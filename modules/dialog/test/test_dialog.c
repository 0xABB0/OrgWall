#include <dialog/dialog.h>
#include <dialog/backend.h>
#include <test/test.h>

#include <future/future.h>
#include <executor/executor.h>
#include <vat/vat.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/list.h>

#include <string.h>

static bool              fake_available = true;
static bool              fake_defer;
static u64               fake_pending_token;
static bool              fake_cancel;
static u32               fake_emit_count = 1;
static u32               fake_chosen_filter;
static Mel_Dialog_Status fake_warn;

bool mel_dialog__plat_available(void) { return fake_available; }

static void fake_emit(Mel_Dialog_Job* job)
{
    if (fake_cancel)
    {
        mel_dialog_job_resolve(job, MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
        return;
    }
    if (fake_warn)
        mel_dialog_job_add_warning(job, fake_warn);
    u32 request = mel_dialog_job_request(job);
    for (u32 i = 0; i < fake_emit_count; i++)
    {
        char path[64];
        if (request & MEL_DIALOG_REQUEST_OPEN_DIR)
            snprintf(path, sizeof path, "/picked/folder%u", i);
        else if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
            snprintf(path, sizeof path, "/picked/save%u.txt", i);
        else
            snprintf(path, sizeof path, "/picked/file%u.txt", i);
        mel_dialog_job_emit_path(job, path);
    }
    mel_dialog_job_set_chosen_filter(job, fake_chosen_filter);
    mel_dialog_job_resolve(job, MEL_DIALOG_OK);
}

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    if (fake_defer)
    {
        fake_pending_token = mel_dialog_job_token(job);
        return;
    }
    fake_emit(job);
}

static void reset_fake(void)
{
    fake_available = true;
    fake_defer = false;
    fake_pending_token = 0;
    fake_cancel = false;
    fake_emit_count = 1;
    fake_chosen_filter = 0;
    fake_warn = 0;
}

MEL_TEST(dialog, available_reports_backend)
{
    reset_fake();
    mel_dialog_init(mel_alloc_heap(), NULL);
    MEL_EXPECT(mel_dialog_available());
    fake_available = false;
    MEL_EXPECT(!mel_dialog_available());
    mel_dialog_shutdown();
}

MEL_TEST(dialog, open_file_single_returns_one_path)
{
    reset_fake();
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_open_file(.title = "Pick one");
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT(mel_dialog_status_ok(sel->status));
    MEL_EXPECT_EQ((i64)sel->path_count, (i64)1);
    MEL_EXPECT_STR_EQ(sel->paths[0], "/picked/file0.txt");
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, open_files_multi_returns_all)
{
    reset_fake();
    fake_emit_count = 3;
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_open_files(.title = "Pick many");
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT_EQ((i64)sel->path_count, (i64)3);
    MEL_EXPECT_STR_EQ(sel->paths[2], "/picked/file2.txt");
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, save_file_carries_default_name)
{
    reset_fake();
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_save_file(.default_name = "draft.txt");
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT_EQ((i64)sel->path_count, (i64)1);
    MEL_EXPECT_STR_EQ(sel->paths[0], "/picked/save0.txt");
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, open_folder_returns_directory)
{
    reset_fake();
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_open_folder(.title = "Pick dir");
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT_EQ((i64)sel->path_count, (i64)1);
    MEL_EXPECT_STR_EQ(sel->paths[0], "/picked/folder0");
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, cancel_yields_empty_selection)
{
    reset_fake();
    fake_cancel = true;
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_open_file();
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT_EQ((i64)sel->path_count, (i64)0);
    MEL_EXPECT(mel_dialog_status_cancelled(sel->status));
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, reports_chosen_filter)
{
    reset_fake();
    fake_chosen_filter = 1;
    mel_dialog_init(mel_alloc_heap(), NULL);

    const char*       png[] = { "*.png" };
    const char*       txt[] = { "*.txt" };
    Mel_Dialog_Filter filters[] = {
        { .label = "Images", .patterns = png, .pattern_count = 1 },
        { .label = "Text", .patterns = txt, .pattern_count = 1 },
    };
    Mel_Future* f = mel_dialog_open_file(.filters = filters, .filter_count = 2);
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT_EQ((i64)sel->chosen_filter, (i64)1);
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, warning_path_sets_warned_severity_and_bit)
{
    reset_fake();
    fake_warn = MEL_DIALOG_WARN_FILTER_IGNORED;
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_open_file();
    MEL_REQUIRE_NOT_NULL(f);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    MEL_REQUIRE_NOT_NULL(sel);
    MEL_EXPECT(mel_dialog_status_warned(sel->status));
    MEL_EXPECT((sel->status & MEL_DIALOG_WARN_FILTER_IGNORED) != 0u);
    MEL_EXPECT(!mel_dialog_status_failed(sel->status));
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

static void test_noop_submit(Mel_Executor* self, Mel_Task* task)
{
    (void)self;
    (void)task;
}

MEL_TEST(dialog, mismatched_deliver_executor_rejected)
{
    reset_fake();
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Executor wrong = { test_noop_submit };
    Mel_Future*  f = mel_dialog_open_file(.deliver = &wrong);
    MEL_REQUIRE_NOT_NULL(f);
    Mel_Dialog_Status st = mel_dialog_future_status(f);
    MEL_EXPECT(mel_dialog_status_failed(st));
    MEL_EXPECT((st & MEL_DIALOG_UNAVAILABLE) != 0u);
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

MEL_TEST(dialog, no_backend_fails_loudly)
{
    reset_fake();
    fake_available = false;
    mel_dialog_init(mel_alloc_heap(), NULL);

    Mel_Future* f = mel_dialog_open_file();
    MEL_REQUIRE_NOT_NULL(f);
    Mel_Dialog_Status st = mel_dialog_future_status(f);
    MEL_EXPECT(mel_dialog_status_failed(st));
    MEL_EXPECT((st & MEL_DIALOG_NO_BACKEND) != 0u);
    mel_dialog_future_free(f);
    mel_dialog_shutdown();
}

typedef struct
{
    bool (*fn)(void* user);
    void* user;
} Idle;

static i64 idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Idle* idle = mel_vat_source_state(s);
    idle->fn(idle->user);
    return false;
}

static const Mel_Vat_Source_Vtbl IDLE_VT = {
    .wakeables = NULL,
    .deadline = idle_deadline,
    .drain = idle_drain,
    .cancel = NULL,
};

static void run_with_dialog_vat(bool (*fn)(void* user), void* user, Mel_Vat** vat_slot)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_io(a);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(a, 64);
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    *vat_slot = vat;
    mel_dialog_init(a, vat);
    Idle            idle = { .fn = fn, .user = user };
    Mel_Vat_Source* src = mel_vat_source_open(vat, &IDLE_VT, &idle);
    mel_vat_run(vat);
    mel_vat_source_close(src);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}

typedef struct
{
    Mel_Vat*          vat;
    int               turn;
    Mel_Task          task;
    Mel_Future*       pending;
    bool              done;
    u32               path_count;
    Mel_Dialog_Status status;
} Async_Ctx;

static void on_picked(Mel_Task* self)
{
    Async_Ctx*                  a = mel_container_of(self, Async_Ctx, task);
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(a->pending);
    a->path_count = sel ? sel->path_count : 0;
    a->status = sel ? sel->status : (MEL_DIALOG_ERROR);
    mel_dialog_future_free(a->pending);
    a->pending = NULL;
    a->done = true;
}

static bool async_idle(void* user)
{
    Async_Ctx* a = (Async_Ctx*)user;
    a->turn++;
    if (a->turn == 2)
    {
        a->pending = mel_dialog_open_file(.vat = a->vat, .deliver = mel_vat_executor(a->vat));
        mel_task_init(&a->task, on_picked);
        mel_future_then(a->pending, &a->task, mel_vat_executor(a->vat));
    }
    if (a->turn == 4 && fake_pending_token)
    {
        Mel_Dialog_Job* job = mel_dialog__job_from_token(fake_pending_token);
        if (job)
        {
            mel_dialog_job_emit_path(job, "/picked/deferred.txt");
            mel_dialog_job_resolve(job, MEL_DIALOG_OK);
        }
        fake_pending_token = 0;
    }
    if (a->done || a->turn > 5000)
    {
        mel_dialog_shutdown();
        mel_vat_quit(a->vat);
    }
    return true;
}

MEL_TEST(dialog, deferred_resolution_delivers_on_executor)
{
    reset_fake();
    fake_defer = true;

    Async_Ctx a = { 0 };
    run_with_dialog_vat(async_idle, &a, &a.vat);

    MEL_EXPECT(a.done);
    MEL_EXPECT_EQ((i64)a.path_count, (i64)1);
    MEL_EXPECT(mel_dialog_status_ok(a.status));
}

typedef struct
{
    Mel_Vat*    vat;
    int         turn;
    Mel_Task    task;
    Mel_Future* pending;
    bool        cont_ran;
    bool        shut;
} Shutdown_Ctx;

static void shutdown_on_picked(Mel_Task* self)
{
    Shutdown_Ctx* c = mel_container_of(self, Shutdown_Ctx, task);
    c->cont_ran = true;
}

static bool shutdown_idle(void* user)
{
    Shutdown_Ctx* c = (Shutdown_Ctx*)user;
    c->turn++;
    if (c->turn == 2)
    {
        c->pending = mel_dialog_open_file(.vat = c->vat, .deliver = mel_vat_executor(c->vat));
        mel_task_init(&c->task, shutdown_on_picked);
        mel_future_then(c->pending, &c->task, mel_vat_executor(c->vat));
    }
    if (c->turn == 4)
    {
        mel_dialog_shutdown();
        c->shut = true;
    }
    if (c->shut || c->turn > 5000)
        mel_vat_quit(c->vat);
    return true;
}

MEL_TEST(dialog, shutdown_with_pending_continuation_does_not_crash_or_leak)
{
    reset_fake();
    fake_defer = true;

    Shutdown_Ctx c = { 0 };
    run_with_dialog_vat(shutdown_idle, &c, &c.vat);

    MEL_EXPECT(c.shut);
    MEL_EXPECT(!c.cont_ran);
}
