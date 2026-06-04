#include <shell/shell.h>
#include <shell/backend.h>
#include <test/test.h>

#include <future/future.h>
#include <executor/executor.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>
#include <collection.list/list.h>
#include <string.h>

static bool  fake_avail = true;
static bool  fake_defer;
static u64   fake_token;
static char  last_target[256];
static usize last_len;
static int   open_calls;
static int   reveal_calls;

bool  mel_shell__plat_available(void) { return fake_avail; }
void* mel_shell__plat_native(void) { return NULL; }

static void record_target(Mel_Shell_Job* j)
{
    str8 t = mel_shell_job_target(j);
    last_len = (usize)t.len < sizeof last_target ? (usize)t.len : sizeof last_target - 1;
    if (last_len && t.data)
        memcpy(last_target, t.data, last_len);
    last_target[last_len] = 0;
}

void mel_shell__plat_open_url(Mel_Shell_Job* j)
{
    open_calls++;
    record_target(j);
    if (fake_defer)
    {
        fake_token = mel_shell_job_token(j);
        return;
    }
    mel_shell_job_resolve(j, MEL_SHELL_OK);
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* j)
{
    reveal_calls++;
    record_target(j);
    if (fake_defer)
    {
        fake_token = mel_shell_job_token(j);
        return;
    }
    mel_shell_job_resolve(j, MEL_SHELL_WARNED | MEL_SHELL_WARN_REVEAL_DEGRADED);
}

static void install_fake(void)
{
    fake_avail = true;
    fake_defer = false;
    fake_token = 0;
    last_len = 0;
    open_calls = 0;
    reveal_calls = 0;
    mel_shell_init(mel_alloc_heap(), NULL);
}

MEL_TEST(shell, open_url_resolves_ok_and_passes_target)
{
    install_fake();
    Mel_Future* f = mel_shell_open_url(S8("https://melody.example"));
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT_EQ(open_calls, 1);
    MEL_EXPECT_EQ_STR8(((str8){ (u8*)last_target, (size)last_len }), S8("https://melody.example"));
    MEL_EXPECT(mel_shell_ok(mel_shell_future_status(f)));
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, reveal_path_routes_to_reveal_backend)
{
    install_fake();
    Mel_Future* f = mel_shell_reveal_path(S8("/tmp/melody/file.txt"));
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT_EQ(reveal_calls, 1);
    MEL_EXPECT_EQ(open_calls, 0);
    MEL_EXPECT(mel_shell_warned(mel_shell_future_status(f)));
    MEL_EXPECT((mel_shell_future_status(f) & MEL_SHELL_WARN_REVEAL_DEGRADED) != 0);
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, empty_target_is_bad_target)
{
    install_fake();
    Mel_Future* f = mel_shell_open_url(STR8_EMPTY);
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT_EQ(open_calls, 0);
    Mel_Shell_Status s = mel_shell_future_status(f);
    MEL_EXPECT(mel_shell_failed(s));
    MEL_EXPECT((s & MEL_SHELL_RESULT_BAD_TARGET) != 0);
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, no_backend_reports_no_backend)
{
    fake_avail = false;
    mel_shell_init(mel_alloc_heap(), NULL);
    MEL_EXPECT(!mel_shell_available());
    Mel_Future* f = mel_shell_open_url(S8("https://x"));
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    Mel_Shell_Status s = mel_shell_future_status(f);
    MEL_EXPECT(mel_shell_failed(s));
    MEL_EXPECT((s & MEL_SHELL_RESULT_NO_BACKEND) != 0);
    mel_shell_future_free(f);
    mel_shell_shutdown();
    fake_avail = true;
}

MEL_TEST(shell, out_op_is_reported_and_valid)
{
    install_fake();
    Mel_Shell_Op op = MEL_SHELL_OP_NULL;
    MEL_EXPECT(!mel_shell_op_valid(op));
    Mel_Future* f = mel_shell_open_url(S8("https://op"), .out_op = &op);
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_shell_op_valid(op));
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, deferred_resolves_via_token)
{
    install_fake();
    fake_defer = true;
    Mel_Future* f = mel_shell_open_url(S8("https://deferred"));
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(!mel_future_resolved(f));
    MEL_REQUIRE(fake_token != 0);

    Mel_Shell_Job* j = mel_shell__job_from_token(fake_token);
    MEL_REQUIRE(j != NULL);
    MEL_EXPECT_EQ_STR8(mel_shell_job_target(j), S8("https://deferred"));
    mel_shell_job_resolve(j, MEL_SHELL_OK);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(mel_shell_ok(mel_shell_future_status(f)));
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, cancel_pending_op)
{
    install_fake();
    fake_defer = true;
    Mel_Shell_Op op = MEL_SHELL_OP_NULL;
    Mel_Future*  f = mel_shell_open_url(S8("https://cancel"), .out_op = &op);
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(!mel_future_resolved(f));
    MEL_EXPECT(mel_shell_cancel(op));
    Mel_Shell_Status s = mel_shell_future_status(f);
    MEL_EXPECT(mel_shell_cancelled(s));
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, cancel_resolved_op_is_false)
{
    install_fake();
    Mel_Shell_Op op = MEL_SHELL_OP_NULL;
    Mel_Future*  f = mel_shell_open_url(S8("https://done"), .out_op = &op);
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(!mel_shell_cancel(op));
    mel_shell_future_free(f);
    mel_shell_shutdown();
}

MEL_TEST(shell, token_invalid_after_free)
{
    install_fake();
    fake_defer = true;
    Mel_Future* f = mel_shell_open_url(S8("https://gone"));
    MEL_REQUIRE(f != NULL);
    u64 tok = fake_token;
    MEL_REQUIRE(tok != 0);
    Mel_Shell_Job* j = mel_shell__job_from_token(tok);
    MEL_REQUIRE(j != NULL);
    mel_shell_job_resolve(j, MEL_SHELL_OK);
    mel_shell_future_free(f);
    MEL_EXPECT(mel_shell__job_from_token(tok) == NULL);
    mel_shell_shutdown();
}

MEL_TEST(shell, shutdown_frees_pending_without_continuation)
{
    install_fake();
    fake_defer = true;
    (void)mel_shell_open_url(S8("https://a"));
    (void)mel_shell_reveal_path(S8("/b"));
    mel_shell_shutdown();
}

typedef struct
{
    Mel_Task         task;
    Mel_Future*      fut;
    int              ran;
    Mel_Shell_Status status;
} Cont;

static void cont_run(Mel_Task* self)
{
    Cont* c = mel_container_of(self, Cont, task);
    c->ran++;
    c->status = mel_shell_future_status(c->fut);
    mel_shell_future_free(c->fut);
}

MEL_TEST(shell, then_delivers_on_inline_executor)
{
    install_fake();
    Mel_Future* f = mel_shell_open_url(S8("https://then"));
    MEL_REQUIRE(f != NULL);
    Cont c = { 0 };
    c.fut = f;
    mel_task_init(&c.task, cont_run);
    mel_future_then(f, &c.task, mel_executor_inline());
    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_shell_ok(c.status));
    mel_shell_shutdown();
}

MEL_TEST(shell, shutdown_cancels_pending_with_continuation)
{
    install_fake();
    fake_defer = true;
    Mel_Future* f = mel_shell_open_url(S8("https://pending"));
    MEL_REQUIRE(f != NULL);
    Cont c = { 0 };
    c.fut = f;
    mel_task_init(&c.task, cont_run);
    mel_future_then(f, &c.task, mel_executor_inline());
    MEL_EXPECT_EQ(c.ran, 0);
    mel_shell_shutdown();
    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_shell_cancelled(c.status));
}

typedef struct
{
    i64 live;
} Counting_Alloc;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user)
{
    (void)file;
    (void)func;
    (void)line;
    Counting_Alloc*  cnt = (Counting_Alloc*)user;
    const Mel_Alloc* heap = mel_alloc_heap();
    if (ptr == NULL)
    {
        cnt->live++;
        return align ? mel_aligned_alloc(heap, size, align) : mel_alloc(heap, size);
    }
    if (size == 0)
    {
        cnt->live--;
        if (align)
            mel_aligned_dealloc(heap, ptr, align);
        else
            mel_dealloc(heap, ptr);
        return NULL;
    }
    return mel_realloc(heap, ptr, size);
}

static void body_success(const Mel_Alloc* a)
{
    fake_avail = true;
    fake_defer = false;
    mel_shell_init(a, NULL);
    mel_shell_future_free(mel_shell_open_url(S8("https://leak")));
    mel_shell_future_free(mel_shell_reveal_path(S8("/leak/path")));
    mel_shell_shutdown();
}

static void body_shutdown_pending(const Mel_Alloc* a)
{
    fake_avail = true;
    fake_defer = true;
    mel_shell_init(a, NULL);
    (void)mel_shell_open_url(S8("https://p1"));
    (void)mel_shell_reveal_path(S8("/p2"));
    mel_shell_shutdown();
}

MEL_TEST(shell, no_leak_success_path)
{
    Counting_Alloc cnt = { 0 };
    Mel_Alloc      tracked = { counting_cb, &cnt };
    body_success(&tracked);
    MEL_EXPECT_EQ(cnt.live, (i64)0);
}

MEL_TEST(shell, no_leak_shutdown_pending_path)
{
    Counting_Alloc cnt = { 0 };
    Mel_Alloc      tracked = { counting_cb, &cnt };
    body_shutdown_pending(&tracked);
    MEL_EXPECT_EQ(cnt.live, (i64)0);
}
