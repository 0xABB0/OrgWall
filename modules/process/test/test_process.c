#include <process/process.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <vat/vat.h>
#include <future/future.h>
#include <executor/executor.h>
#include <io/stream.h>
#include <test/test.h>

#include <collection/list.h>

#include <string.h>
#include <unistd.h>

static const char* find_bin(const char* a, const char* b)
{
    if (access(a, X_OK) == 0)
        return a;
    if (access(b, X_OK) == 0)
        return b;
    return NULL;
}

MEL_TEST(process, available_on_host) { MEL_EXPECT(mel_process_available()); }

MEL_TEST(process, spawn_true_exits_zero)
{
    const char* tru = find_bin("/usr/bin/true", "/bin/true");
    MEL_REQUIRE_NOT_NULL(tru);

    const char*              argv[] = { tru, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1, .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL }, .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
    MEL_REQUIRE(mel_process_status_ok(sr.status));
    MEL_REQUIRE_NOT_NULL(sr.value);
    MEL_EXPECT(mel_process_pid(sr.value) > 0);

    Mel_Process_Exit ex = mel_process_wait_sync(sr.value);
    MEL_EXPECT(mel_process_status_exited(ex.status));
    MEL_EXPECT_EQ(ex.exit_code, 0);
    MEL_EXPECT_EQ(mel_process_running(sr.value), false);

    mel_process_destroy(sr.value);
}

MEL_TEST(process, spawn_false_exits_nonzero)
{
    const char* fls = find_bin("/usr/bin/false", "/bin/false");
    MEL_REQUIRE_NOT_NULL(fls);

    const char*              argv[] = { fls, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1, .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL }, .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
    MEL_REQUIRE(mel_process_status_ok(sr.status));

    Mel_Process_Exit ex = mel_process_wait_sync(sr.value);
    MEL_EXPECT(mel_process_status_exited(ex.status));
    MEL_EXPECT_NEQ(ex.exit_code, 0);
    mel_process_destroy(sr.value);
}

MEL_TEST(process, missing_binary_fails_not_found)
{
    const char*              argv[] = { "/nonexistent/melody/process/binary", NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1, .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL }, .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
    MEL_EXPECT(mel_process_status_failed(sr.status));
    MEL_EXPECT((sr.status & MEL_PROCESS_NOT_FOUND) != 0u);
    MEL_EXPECT_NULL(sr.value);
}

MEL_TEST(process, spawn_requires_argv)
{
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = NULL, .argc = 0);
    MEL_EXPECT(mel_process_status_failed(sr.status));
    MEL_EXPECT_NULL(sr.value);
}

MEL_TEST(process, kill_terminates_sleeper)
{
    const char* slp = find_bin("/bin/sleep", "/usr/bin/sleep");
    MEL_REQUIRE_NOT_NULL(slp);

    const char*              argv[] = { slp, "30", NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 2, .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL }, .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
    MEL_REQUIRE(mel_process_status_ok(sr.status));
    MEL_EXPECT(mel_process_running(sr.value));

    Mel_Process_Status ks = mel_process_kill(sr.value, .signal = MEL_PROCESS_SIGNAL_KILL);
    MEL_EXPECT(mel_process_status_ok(ks));

    Mel_Process_Exit ex = mel_process_wait_sync(sr.value);
    MEL_EXPECT(mel_process_status_signalled(ex.status));
    mel_process_destroy(sr.value);
}

MEL_TEST(process, detached_mode_marks_status)
{
    const char* tru = find_bin("/usr/bin/true", "/bin/true");
    MEL_REQUIRE_NOT_NULL(tru);

    const char*              argv[] = { tru, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1, .detached = true);
    MEL_REQUIRE(mel_process_status_ok(sr.status));
    MEL_EXPECT((sr.status & MEL_PROCESS_DETACHED) != 0u);
    MEL_EXPECT(mel_process_detached(sr.value));

    Mel_Process_Exit ex = mel_process_wait_sync(sr.value);
    MEL_EXPECT(mel_process_status_failed(ex.status));
    MEL_EXPECT((ex.status & MEL_PROCESS_DETACHED) != 0u);
    mel_process_destroy(sr.value);
}

MEL_TEST(process, detached_rejects_pipe)
{
    const char* tru = find_bin("/usr/bin/true", "/bin/true");
    MEL_REQUIRE_NOT_NULL(tru);
    const char*              argv[] = { tru, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1, .detached = true, .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_PIPE });
    MEL_EXPECT(mel_process_status_failed(sr.status));
    MEL_EXPECT_NULL(sr.value);
}

typedef struct
{
    bool (*fn)(void* user);
    void* user;
} Idle_Body;

static i64 idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Idle_Body* body = mel_vat_source_state(s);
    body->fn(body->user);
    return false;
}

static const Mel_Vat_Source_Vtbl IDLE_VT = {
    .wakeables = NULL,
    .deadline = idle_deadline,
    .drain = idle_drain,
    .cancel = NULL,
};

static void run_idle_on_vat(bool (*fn)(void* user), void* user, Mel_Vat** out_vat)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat_Waiter*  waiter = mel_vat_waiter_io(a);
    Mel_Vat_Driver*  driver = mel_vat_driver_fair(a, 64);
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });
    *out_vat = vat;
    Idle_Body       body = { fn, user };
    Mel_Vat_Source* idle = mel_vat_source_open(vat, &IDLE_VT, &body);
    mel_vat_run(vat);
    mel_vat_source_close(idle);
    mel_vat_close(vat);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
}

typedef struct
{
    Mel_Vat*    vat;
    int         turn;
    Mel_Task    task;
    Mel_Future* pending;
    bool        started;
    bool        done;

    char               out[256];
    usize              out_len;
    int                exit_code;
    Mel_Process_Status status;
    const char*        sh;
} Run_Test;

static void on_run(Mel_Task* self)
{
    Run_Test*                 t = mel_container_of(self, Run_Test, task);
    const Mel_Process_Output* o = mel_process_run_future_result(t->pending);
    t->status = o->status;
    t->exit_code = o->exit_code;
    t->out_len = o->stdout_len;
    if (o->stdout_len > 0 && o->stdout_len < sizeof t->out)
        memcpy(t->out, o->stdout_data, o->stdout_len);
    mel_process_run_future_release(t->pending);
    t->pending = NULL;
    t->done = true;
}

static bool run_idle(void* user)
{
    Run_Test* t = (Run_Test*)user;
    t->turn++;
    if (!t->started)
    {
        t->started = true;
        const char* argv[] = { t->sh, "-c", "printf hello-process", NULL };
        t->pending = mel_process_run(.argv = argv, .argc = 3, .vat = t->vat, .deliver = mel_vat_executor(t->vat));
        if (!t->pending)
        {
            mel_vat_quit(t->vat);
            return true;
        }
        mel_task_init(&t->task, on_run);
        mel_future_then(t->pending, &t->task, mel_vat_executor(t->vat));
    }
    if (t->done)
        mel_vat_quit(t->vat);
    if (t->turn > 200000)
        mel_vat_quit(t->vat);
    return true;
}

MEL_TEST(process, run_collects_stdout_and_exit)
{
    const char* sh = find_bin("/bin/sh", "/usr/bin/sh");
    MEL_REQUIRE_NOT_NULL(sh);

    Run_Test t = { 0 };
    t.sh = sh;
    run_idle_on_vat(run_idle, &t, &t.vat);

    MEL_EXPECT(t.done);
    MEL_EXPECT(mel_process_status_ok(t.status) || mel_process_status_exited(t.status));
    MEL_EXPECT_EQ(t.exit_code, 0);
    MEL_EXPECT_EQ((i64)t.out_len, (i64)strlen("hello-process"));
    MEL_EXPECT(memcmp(t.out, "hello-process", strlen("hello-process")) == 0);
}

typedef struct
{
    Mel_Vat*           vat;
    int                turn;
    Mel_Task           task;
    Mel_Future*        pending;
    bool               started;
    bool               done;
    char               out[64];
    usize              out_len;
    Mel_Process_Status status;
    const char*        cat;
} Stdin_Test;

static void on_stdin(Mel_Task* self)
{
    Stdin_Test*               t = mel_container_of(self, Stdin_Test, task);
    const Mel_Process_Output* o = mel_process_run_future_result(t->pending);
    t->status = o->status;
    t->out_len = o->stdout_len;
    if (o->stdout_len > 0 && o->stdout_len < sizeof t->out)
        memcpy(t->out, o->stdout_data, o->stdout_len);
    mel_process_run_future_release(t->pending);
    t->pending = NULL;
    t->done = true;
}

static bool stdin_idle(void* user)
{
    Stdin_Test* t = (Stdin_Test*)user;
    t->turn++;
    if (!t->started)
    {
        t->started = true;
        const char* argv[] = { t->cat, NULL };
        const char* payload = "piped-stdin-payload";
        t->pending = mel_process_run(.argv = argv, .argc = 1, .stdin_data = payload, .stdin_len = strlen(payload), .vat = t->vat, .deliver = mel_vat_executor(t->vat));
        if (!t->pending)
        {
            mel_vat_quit(t->vat);
            return true;
        }
        mel_task_init(&t->task, on_stdin);
        mel_future_then(t->pending, &t->task, mel_vat_executor(t->vat));
    }
    if (t->done)
        mel_vat_quit(t->vat);
    if (t->turn > 200000)
        mel_vat_quit(t->vat);
    return true;
}

MEL_TEST(process, run_feeds_stdin_through_cat)
{
    const char* cat = find_bin("/bin/cat", "/usr/bin/cat");
    MEL_REQUIRE_NOT_NULL(cat);

    Stdin_Test t = { 0 };
    t.cat = cat;
    run_idle_on_vat(stdin_idle, &t, &t.vat);

    MEL_EXPECT(t.done);
    MEL_EXPECT_EQ((i64)t.out_len, (i64)strlen("piped-stdin-payload"));
    MEL_EXPECT(memcmp(t.out, "piped-stdin-payload", strlen("piped-stdin-payload")) == 0);
}

typedef struct
{
    Mel_Vat*    vat;
    int         turn;
    Mel_Task    task;
    Mel_Future* pending;
    bool        started;
    bool        done;
    char        out[64];
    usize       out_len;
    const char* sh;
} Env_Test;

static void on_env(Mel_Task* self)
{
    Env_Test*                 t = mel_container_of(self, Env_Test, task);
    const Mel_Process_Output* o = mel_process_run_future_result(t->pending);
    t->out_len = o->stdout_len;
    if (o->stdout_len > 0 && o->stdout_len < sizeof t->out)
        memcpy(t->out, o->stdout_data, o->stdout_len);
    mel_process_run_future_release(t->pending);
    t->pending = NULL;
    t->done = true;
}

static bool env_idle(void* user)
{
    Env_Test* t = (Env_Test*)user;
    t->turn++;
    if (!t->started)
    {
        t->started = true;
        const char*               argv[] = { t->sh, "-c", "printf %s \"$MEL_PROCESS_TESTVAR\"", NULL };
        const Mel_Process_Env_Var env[] = { { "MEL_PROCESS_TESTVAR", "env-ok" } };
        t->pending = mel_process_run(.argv = argv, .argc = 3, .env = env, .env_count = 1, .vat = t->vat, .deliver = mel_vat_executor(t->vat));
        if (!t->pending)
        {
            mel_vat_quit(t->vat);
            return true;
        }
        mel_task_init(&t->task, on_env);
        mel_future_then(t->pending, &t->task, mel_vat_executor(t->vat));
    }
    if (t->done)
        mel_vat_quit(t->vat);
    if (t->turn > 200000)
        mel_vat_quit(t->vat);
    return true;
}

MEL_TEST(process, run_passes_env_var)
{
    const char* sh = find_bin("/bin/sh", "/usr/bin/sh");
    MEL_REQUIRE_NOT_NULL(sh);

    Env_Test t = { 0 };
    t.sh = sh;
    run_idle_on_vat(env_idle, &t, &t.vat);

    MEL_EXPECT(t.done);
    MEL_EXPECT_EQ((i64)t.out_len, (i64)strlen("env-ok"));
    MEL_EXPECT(memcmp(t.out, "env-ok", strlen("env-ok")) == 0);
}
