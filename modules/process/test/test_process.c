#include <process/process.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <reactor/reactor.h>
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

MEL_TEST(process, available_on_host)
{
    MEL_EXPECT(mel_process_available());
}

MEL_TEST(process, spawn_true_exits_zero)
{
    const char* tru = find_bin("/usr/bin/true", "/bin/true");
    MEL_REQUIRE_NOT_NULL(tru);

    const char* argv[] = { tru, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1,
                                                    .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL },
                                                    .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
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

    const char* argv[] = { fls, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1,
                                                    .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL },
                                                    .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
    MEL_REQUIRE(mel_process_status_ok(sr.status));

    Mel_Process_Exit ex = mel_process_wait_sync(sr.value);
    MEL_EXPECT(mel_process_status_exited(ex.status));
    MEL_EXPECT_NEQ(ex.exit_code, 0);
    mel_process_destroy(sr.value);
}

MEL_TEST(process, missing_binary_fails_not_found)
{
    const char* argv[] = { "/nonexistent/melody/process/binary", NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1,
                                                    .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL },
                                                    .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
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

    const char* argv[] = { slp, "30", NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 2,
                                                    .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_NULL },
                                                    .stderr_cfg = { .disposition = MEL_PROCESS_STDIO_NULL });
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

    const char* argv[] = { tru, NULL };
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
    const char* argv[] = { tru, NULL };
    Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = argv, .argc = 1, .detached = true,
                                                    .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_PIPE });
    MEL_EXPECT(mel_process_status_failed(sr.status));
    MEL_EXPECT_NULL(sr.value);
}

typedef struct
{
    Mel_Reactor* reactor;
    int          turn;
    Mel_Task     task;
    Mel_Future*  pending;
    bool         started;
    bool         done;

    char  out[256];
    usize out_len;
    int   exit_code;
    Mel_Process_Status status;
    const char* sh;
} Run_Test;

static void on_run(Mel_Task* self)
{
    Run_Test* t = mel_container_of(self, Run_Test, task);
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
        t->pending = mel_process_run(.argv = argv, .argc = 3, .reactor = t->reactor, .deliver = mel_reactor_executor(t->reactor));
        if (!t->pending)
        {
            mel_reactor_quit(t->reactor);
            return true;
        }
        mel_task_init(&t->task, on_run);
        mel_future_then(t->pending, &t->task, mel_reactor_executor(t->reactor));
    }
    if (t->done)
        mel_reactor_quit(t->reactor);
    if (t->turn > 200000)
        mel_reactor_quit(t->reactor);
    return true;
}

static bool run_init(Mel_Reactor* r, void* user)
{
    Run_Test* t = (Run_Test*)user;
    t->reactor = r;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(run_idle, t);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(process, run_collects_stdout_and_exit)
{
    const char* sh = find_bin("/bin/sh", "/usr/bin/sh");
    MEL_REQUIRE_NOT_NULL(sh);

    Run_Test t = { 0 };
    t.sh = sh;
    mel_reactor_spawn(MEL_REACTOR_THREADED, run_init, &t);

    MEL_EXPECT(t.done);
    MEL_EXPECT(mel_process_status_ok(t.status) || mel_process_status_exited(t.status));
    MEL_EXPECT_EQ(t.exit_code, 0);
    MEL_EXPECT_EQ((i64)t.out_len, (i64)strlen("hello-process"));
    MEL_EXPECT(memcmp(t.out, "hello-process", strlen("hello-process")) == 0);
}

typedef struct
{
    Mel_Reactor* reactor;
    int          turn;
    Mel_Task     task;
    Mel_Future*  pending;
    bool         started;
    bool         done;
    char         out[64];
    usize        out_len;
    Mel_Process_Status status;
    const char*  cat;
} Stdin_Test;

static void on_stdin(Mel_Task* self)
{
    Stdin_Test* t = mel_container_of(self, Stdin_Test, task);
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
        t->pending = mel_process_run(.argv = argv, .argc = 1, .stdin_data = payload, .stdin_len = strlen(payload), .reactor = t->reactor, .deliver = mel_reactor_executor(t->reactor));
        if (!t->pending)
        {
            mel_reactor_quit(t->reactor);
            return true;
        }
        mel_task_init(&t->task, on_stdin);
        mel_future_then(t->pending, &t->task, mel_reactor_executor(t->reactor));
    }
    if (t->done)
        mel_reactor_quit(t->reactor);
    if (t->turn > 200000)
        mel_reactor_quit(t->reactor);
    return true;
}

static bool stdin_init(Mel_Reactor* r, void* user)
{
    Stdin_Test* t = (Stdin_Test*)user;
    t->reactor = r;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(stdin_idle, t);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(process, run_feeds_stdin_through_cat)
{
    const char* cat = find_bin("/bin/cat", "/usr/bin/cat");
    MEL_REQUIRE_NOT_NULL(cat);

    Stdin_Test t = { 0 };
    t.cat = cat;
    mel_reactor_spawn(MEL_REACTOR_THREADED, stdin_init, &t);

    MEL_EXPECT(t.done);
    MEL_EXPECT_EQ((i64)t.out_len, (i64)strlen("piped-stdin-payload"));
    MEL_EXPECT(memcmp(t.out, "piped-stdin-payload", strlen("piped-stdin-payload")) == 0);
}

typedef struct
{
    Mel_Reactor* reactor;
    int          turn;
    Mel_Task     task;
    Mel_Future*  pending;
    bool         started;
    bool         done;
    char         out[64];
    usize        out_len;
    const char*  sh;
} Env_Test;

static void on_env(Mel_Task* self)
{
    Env_Test* t = mel_container_of(self, Env_Test, task);
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
        t->pending = mel_process_run(.argv = argv, .argc = 3, .env = env, .env_count = 1, .reactor = t->reactor, .deliver = mel_reactor_executor(t->reactor));
        if (!t->pending)
        {
            mel_reactor_quit(t->reactor);
            return true;
        }
        mel_task_init(&t->task, on_env);
        mel_future_then(t->pending, &t->task, mel_reactor_executor(t->reactor));
    }
    if (t->done)
        mel_reactor_quit(t->reactor);
    if (t->turn > 200000)
        mel_reactor_quit(t->reactor);
    return true;
}

static bool env_init(Mel_Reactor* r, void* user)
{
    Env_Test* t = (Env_Test*)user;
    t->reactor = r;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(env_idle, t);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(process, run_passes_env_var)
{
    const char* sh = find_bin("/bin/sh", "/usr/bin/sh");
    MEL_REQUIRE_NOT_NULL(sh);

    Env_Test t = { 0 };
    t.sh = sh;
    mel_reactor_spawn(MEL_REACTOR_THREADED, env_init, &t);

    MEL_EXPECT(t.done);
    MEL_EXPECT_EQ((i64)t.out_len, (i64)strlen("env-ok"));
    MEL_EXPECT(memcmp(t.out, "env-ok", strlen("env-ok")) == 0);
}
