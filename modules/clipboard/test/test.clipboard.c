#include <clipboard/clipboard.h>
#include <clipboard/backend.h>
#include <test/test.h>

#include <future/future.h>
#include <event/event.h>
#include <executor/executor.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/list.h>
#include <string/str8.h>
#include <string.h>

typedef struct
{
    char  text[256];
    usize len;
    u64   seq;
} Fake_Channel;

static Fake_Channel fake_clip;
static Fake_Channel fake_prim;
static bool         fake_avail = true;
static bool         fake_primary_supported = true;
static bool         fake_defer_read;
static u64          fake_pending_token;

static Fake_Channel* fake_chan(Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY ? &fake_prim : &fake_clip; }

#define fake_text fake_clip.text
#define fake_len  fake_clip.len
#define fake_seq  fake_clip.seq

typedef struct
{
    i64 live;
    i64 net_bytes;
} Counting_Alloc;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user)
{
    Counting_Alloc*  cnt = (Counting_Alloc*)user;
    const Mel_Alloc* heap = mel_alloc_heap();
    if (ptr == NULL)
    {
        cnt->live++;
        cnt->net_bytes += (i64)size;
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

bool mel_clip__plat_available(void) { return fake_avail; }

void mel_clip__plat_shutdown(void) {}

bool mel_clip__plat_channel_supported(Mel_Clip_Channel ch)
{
    if (mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY)
        return fake_primary_supported;
    return true;
}

u64   mel_clip__plat_sequence(Mel_Clip_Channel ch) { return mel_clip__plat_channel_supported(ch) ? fake_chan(ch)->seq : 0; }
void* mel_clip__plat_native(void) { return NULL; }

void mel_clip__plat_write(Mel_Clip_Job* j)
{
    if (!mel_clip__plat_channel_supported(mel_clip_job_channel(j)))
    {
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    Fake_Channel* c = fake_chan(mel_clip_job_channel(j));
    c->len = 0;
    u32 reps = mel_clip_job_rep_count(j, 0);
    for (u32 r = 0; r < reps; r++)
    {
        Mel_Clip_Rep rep = mel_clip_job_rep(j, 0, r);
        if (rep.format == MEL_CLIP_FMT_TEXT)
        {
            usize n = (usize)rep.bytes.len < sizeof c->text ? (usize)rep.bytes.len : sizeof c->text - 1;
            memcpy(c->text, rep.bytes.data, n);
            c->len = n;
        }
    }
    c->seq++;
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

void mel_clip__plat_read(Mel_Clip_Job* j)
{
    if (!mel_clip__plat_channel_supported(mel_clip_job_channel(j)))
    {
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    if (fake_defer_read)
    {
        fake_pending_token = mel_clip_job_token(j);
        return;
    }
    Fake_Channel* c = fake_chan(mel_clip_job_channel(j));
    if (c->len)
        mel_clip_job_emit(j, MEL_CLIP_FMT_TEXT, c->text, c->len);
    mel_clip_job_resolve(j, c->len ? MEL_CLIP_OK : (MEL_CLIP_RESULT_EMPTY));
}

void mel_clip__plat_clear(Mel_Clip_Job* j)
{
    Fake_Channel* c = fake_chan(mel_clip_job_channel(j));
    c->len = 0;
    c->seq++;
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

void mel_clip__plat_query(Mel_Clip_Job* j)
{
    Fake_Channel* c = fake_chan(mel_clip_job_channel(j));
    if (c->len)
        mel_clip_job_emit_format(j, MEL_CLIP_FMT_TEXT);
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

void mel_clip__plat_has(Mel_Clip_Job* j)
{
    if (!mel_clip__plat_channel_supported(mel_clip_job_channel(j)))
    {
        mel_clip_job_set_present(j, false);
        mel_clip_job_resolve(j, MEL_CLIP_OK);
        return;
    }
    Fake_Channel* c = fake_chan(mel_clip_job_channel(j));
    mel_clip_job_set_present(j, c->len > 0);
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

static void install_fake(void)
{
    fake_avail = true;
    fake_primary_supported = true;
    fake_clip = (Fake_Channel){ 0 };
    fake_prim = (Fake_Channel){ 0 };
    fake_defer_read = false;
    fake_pending_token = 0;
    mel_clip_init(mel_alloc_heap(), NULL);
}

MEL_TEST(clipboard, well_known_formats_have_canonical_mime)
{
    install_fake();
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(MEL_CLIP_FMT_TEXT), S8("text/plain;charset=utf-8"));
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(MEL_CLIP_FMT_HTML), S8("text/html"));
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(MEL_CLIP_FMT_PNG), S8("image/png"));
    mel_clip_shutdown();
}

MEL_TEST(clipboard, register_dedups_and_is_stable)
{
    install_fake();
    Mel_Clip_Format a = mel_clip_format_register(S8("application/x-mel-test"));
    Mel_Clip_Format b = mel_clip_format_register(S8("application/x-mel-test"));
    MEL_EXPECT_EQ(a, b);
    MEL_EXPECT_NEQ(a, MEL_CLIP_FMT_NONE);
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(a), S8("application/x-mel-test"));
    mel_clip_shutdown();
}

MEL_TEST(clipboard, write_then_read_text_roundtrips)
{
    install_fake();

    Mel_Future* w = mel_clip_write_text(S8("hello melody"));
    MEL_REQUIRE(w != NULL);
    MEL_EXPECT(mel_future_resolved(w));
    MEL_EXPECT_EQ(mel_clip_future_status(w) & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_OK);
    mel_clip_future_free(w);

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT(mel_future_resolved(r));
    MEL_EXPECT_EQ(mel_clip_future_status(r) & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_OK);
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(r), S8("hello melody"));
    mel_clip_future_free(r);

    mel_clip_shutdown();
}

MEL_TEST(clipboard, read_returns_transferable_with_text_rep)
{
    install_fake();
    mel_clip_future_free(mel_clip_write_text(S8("payload")));

    Mel_Future* r = mel_clip_read(NULL, 0);
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT(mel_future_resolved(r));
    const Mel_Clip_Transferable* t = mel_clip_future_transferable(r);
    MEL_REQUIRE(t != NULL);
    MEL_EXPECT_EQ(t->items.count, (usize)1);
    MEL_EXPECT_EQ(t->items.items[0].reps.count, (usize)1);
    MEL_EXPECT_EQ(t->items.items[0].reps.items[0].format, (Mel_Clip_Format)MEL_CLIP_FMT_TEXT);
    MEL_EXPECT_EQ_STR8(t->items.items[0].reps.items[0].bytes, S8("payload"));
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, query_reports_text_format)
{
    install_fake();
    mel_clip_future_free(mel_clip_write_text(S8("z")));

    Mel_Future* q = mel_clip_query();
    MEL_REQUIRE(q != NULL);
    MEL_EXPECT(mel_future_resolved(q));
    Mel_Clip_Formats fmts = mel_clip_future_formats(q);
    MEL_EXPECT_EQ(fmts.count, (u32)1);
    MEL_EXPECT_EQ(fmts.items[0], (Mel_Clip_Format)MEL_CLIP_FMT_TEXT);
    mel_clip_future_free(q);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, clear_empties_clipboard)
{
    install_fake();
    mel_clip_future_free(mel_clip_write_text(S8("gone")));

    Mel_Future* c = mel_clip_clear();
    MEL_REQUIRE(c != NULL);
    MEL_EXPECT(mel_future_resolved(c));
    MEL_EXPECT_EQ(mel_clip_future_status(c) & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_OK);
    mel_clip_future_free(c);

    Mel_Future* r = mel_clip_read_text();
    MEL_EXPECT((mel_clip_future_status(r) & MEL_CLIP_RESULT_EMPTY) != 0);
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, sequence_advances_on_write)
{
    install_fake();
    u64 before = mel_clip_sequence();
    mel_clip_future_free(mel_clip_write_text(S8("x")));
    MEL_EXPECT_GT(mel_clip_sequence(), before);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, empty_clipboard_reports_empty)
{
    install_fake();
    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT((mel_clip_future_status(r) & MEL_CLIP_RESULT_EMPTY) != 0);
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, no_backend_reports_no_clipboard)
{
    fake_avail = false;
    mel_clip_init(mel_alloc_heap(), NULL);
    MEL_EXPECT(!mel_clip_available());

    Mel_Future* w = mel_clip_write_text(S8("nope"));
    MEL_REQUIRE(w != NULL);
    MEL_EXPECT(mel_future_resolved(w));
    MEL_EXPECT(mel_clip_failed(mel_clip_future_status(w)));
    MEL_EXPECT((mel_clip_future_status(w) & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_future_free(w);

    mel_clip_shutdown();
    fake_avail = true;
}

typedef struct
{
    Mel_Task    task;
    Mel_Future* fut;
    int         ran;
    char        text[64];
    usize       len;
} Read_Cont;

static void read_cont_run(Mel_Task* self)
{
    Read_Cont* c = mel_container_of(self, Read_Cont, task);
    c->ran++;
    str8 t = mel_clip_future_text(c->fut);
    c->len = (usize)t.len < sizeof c->text ? (usize)t.len : sizeof c->text - 1;
    if (c->len && t.data)
        memcpy(c->text, t.data, c->len);
    mel_clip_future_free(c->fut);
}

MEL_TEST(clipboard, then_delivers_on_inline_executor)
{
    install_fake();
    mel_clip_future_free(mel_clip_write_text(S8("via then")));

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);

    Read_Cont c = { 0 };
    c.fut = r;
    mel_task_init(&c.task, read_cont_run);
    mel_future_then(r, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT_EQ_STR8(((str8){ (u8*)c.text, (size)c.len }), S8("via then"));
    mel_clip_shutdown();
}

MEL_TEST(clipboard, transferable_build_and_free)
{
    install_fake();
    Mel_Clip_Transferable t;
    mel_clip_transferable_init(&t, mel_alloc_heap());
    Mel_Clip_Item* it = mel_clip_item_add(&t);
    mel_clip_rep_add(it, MEL_CLIP_FMT_TEXT, S8("a"), t.alloc);
    mel_clip_rep_add(it, MEL_CLIP_FMT_HTML, S8("<b>a</b>"), t.alloc);
    MEL_EXPECT_EQ(t.items.count, (usize)1);
    MEL_EXPECT_EQ(t.items.items[0].reps.count, (usize)2);
    mel_clip_transferable_free(&t);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, write_custom_transferable_roundtrips)
{
    install_fake();
    Mel_Clip_Transferable t;
    mel_clip_transferable_init(&t, mel_alloc_heap());
    Mel_Clip_Item* it = mel_clip_item_add(&t);
    mel_clip_rep_add(it, MEL_CLIP_FMT_TEXT, S8("structured"), t.alloc);

    Mel_Future* w = mel_clip_write(&t);
    MEL_REQUIRE(w != NULL);
    MEL_EXPECT_EQ(mel_clip_future_status(w) & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_OK);
    mel_clip_future_free(w);
    mel_clip_transferable_free(&t);

    Mel_Future* r = mel_clip_read_text();
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(r), S8("structured"));
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, watch_channel_carries_sequence)
{
    install_fake();
    fake_seq = 10;

    Mel_Event* ev = mel_clip_watch();
    MEL_REQUIRE(ev != NULL);
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, NULL);

    u64 change = 11;
    mel_event_fire(ev, &change);

    u64 got = 0;
    MEL_EXPECT(mel_event_pull(ev, sub, &got));
    MEL_EXPECT_EQ(got, (u64)11);
    MEL_EXPECT(!mel_event_pull(ev, sub, &got));

    mel_event_unsubscribe(ev, sub);
    mel_clip_unwatch();
    mel_clip_shutdown();
}

MEL_TEST(clipboard, watch_unsupported_when_no_sequence)
{
    install_fake();
    fake_seq = 0;
    MEL_EXPECT(mel_clip_watch() == NULL);
    mel_clip_shutdown();
}

typedef struct
{
    Mel_Task        task;
    Mel_Future*     fut;
    int             ran;
    Mel_Clip_Status status;
} Cancel_Cont;

static void cancel_cont_run(Mel_Task* self)
{
    Cancel_Cont* c = mel_container_of(self, Cancel_Cont, task);
    c->ran++;
    c->status = mel_clip_future_status(c->fut);
    mel_clip_future_free(c->fut);
}

MEL_TEST(clipboard, shutdown_cancels_pending_with_continuation)
{
    install_fake();
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT(!mel_future_resolved(r));

    Cancel_Cont c = { 0 };
    c.fut = r;
    mel_task_init(&c.task, cancel_cont_run);
    mel_future_then(r, &c.task, mel_executor_inline());
    MEL_EXPECT_EQ(c.ran, 0);

    mel_clip_shutdown();

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_clip_failed(c.status));
    MEL_EXPECT((c.status & MEL_CLIP_RESULT_CANCELLED) != 0);
}

MEL_TEST(clipboard, shutdown_frees_pending_without_continuation)
{
    install_fake();
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT(!mel_future_resolved(r));

    mel_clip_shutdown();
}

MEL_TEST(clipboard, token_recovers_job_and_resolves)
{
    install_fake();
    memcpy(fake_text, "deferred", 8);
    fake_len = 8;
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT(!mel_future_resolved(r));
    MEL_REQUIRE(fake_pending_token != 0);

    Mel_Clip_Job* j = mel_clip__job_from_token(fake_pending_token);
    MEL_REQUIRE(j != NULL);
    mel_clip_job_emit(j, MEL_CLIP_FMT_TEXT, fake_text, fake_len);
    mel_clip_job_resolve(j, MEL_CLIP_OK);

    MEL_EXPECT(mel_future_resolved(r));
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(r), S8("deferred"));
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, denied_status_not_misread_as_cancelled)
{
    install_fake();
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    Mel_Clip_Job* j = mel_clip__job_from_token(fake_pending_token);
    MEL_REQUIRE(j != NULL);
    mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_DENIED);

    Mel_Clip_Status s = mel_clip_future_status(r);
    MEL_EXPECT((s & MEL_CLIP_RESULT_DENIED) != 0);
    MEL_EXPECT((s & MEL_CLIP_RESULT_CANCELLED) == 0);
    MEL_EXPECT(mel_clip_failed(s));
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, status_bits_round_trip_through_future)
{
    install_fake();
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    Mel_Clip_Job* j = mel_clip__job_from_token(fake_pending_token);
    MEL_REQUIRE(j != NULL);
    mel_clip_job_resolve(j, MEL_CLIP_WARNED | MEL_CLIP_RESULT_NO_CLIPBOARD | MEL_CLIP_RESULT_EMPTY | MEL_CLIP_WARN_FORMAT_UNAVAILABLE);

    Mel_Clip_Status s = mel_clip_future_status(r);
    MEL_EXPECT_EQ(s & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_WARNED);
    MEL_EXPECT((s & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    MEL_EXPECT((s & MEL_CLIP_RESULT_EMPTY) != 0);
    MEL_EXPECT((s & MEL_CLIP_WARN_FORMAT_UNAVAILABLE) != 0);
    MEL_EXPECT((s & MEL_CLIP_RESULT_CANCELLED) == 0);
    MEL_EXPECT(mel_future_resolved(r));
    MEL_EXPECT(!mel_future_status_cancelled(mel_future_status(r)));
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, future_severity_only_no_clip_bits_leak)
{
    install_fake();
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    Mel_Clip_Job* j = mel_clip__job_from_token(fake_pending_token);
    MEL_REQUIRE(j != NULL);
    mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_DENIED);

    Mel_Future_Status fs = mel_future_status(r);
    MEL_EXPECT_EQ(fs, (Mel_Future_Status)MEL_FUTURE_ERROR);
    MEL_EXPECT_EQ(fs & ~MEL_FUTURE_SEVERITY_MASK, (Mel_Future_Status)0);
    mel_clip_future_free(r);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, token_invalid_after_free)
{
    install_fake();
    fake_defer_read = true;

    Mel_Future* r = mel_clip_read_text();
    MEL_REQUIRE(r != NULL);
    u64 tok = fake_pending_token;
    MEL_REQUIRE(tok != 0);

    Mel_Clip_Job* j = mel_clip__job_from_token(tok);
    MEL_REQUIRE(j != NULL);
    mel_clip_job_resolve(j, MEL_CLIP_RESULT_EMPTY);
    mel_clip_future_free(r);

    MEL_EXPECT(mel_clip__job_from_token(tok) == NULL);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, channel_resolve_defaults_to_clipboard)
{
    install_fake();
    MEL_EXPECT_EQ(mel_clip_channel_resolve(0), (Mel_Clip_Channel)MEL_CLIP_CHANNEL_CLIPBOARD);
    MEL_EXPECT_EQ(mel_clip_channel_resolve(MEL_CLIP_CHANNEL_PRIMARY), (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, primary_and_clipboard_are_independent_channels)
{
    install_fake();

    mel_clip_future_free(mel_clip_write_text(S8("on-clipboard")));
    mel_clip_future_free(mel_clip_write_text(S8("on-primary"), .channel = MEL_CLIP_CHANNEL_PRIMARY));

    Mel_Future* rc = mel_clip_read_text();
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(rc), S8("on-clipboard"));
    mel_clip_future_free(rc);

    Mel_Future* rp = mel_clip_read_text(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(rp), S8("on-primary"));
    mel_clip_future_free(rp);

    mel_clip_shutdown();
}

MEL_TEST(clipboard, primary_write_does_not_disturb_clipboard)
{
    install_fake();
    mel_clip_future_free(mel_clip_write_text(S8("keep me")));
    mel_clip_future_free(mel_clip_write_text(S8("middle click"), .channel = MEL_CLIP_CHANNEL_PRIMARY));

    Mel_Future* rc = mel_clip_read_text();
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(rc), S8("keep me"));
    mel_clip_future_free(rc);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, has_reports_presence_per_channel)
{
    install_fake();

    Mel_Future* h0 = mel_clip_has();
    MEL_REQUIRE(h0 != NULL);
    MEL_EXPECT(mel_future_resolved(h0));
    MEL_EXPECT(!mel_clip_future_has(h0));
    mel_clip_future_free(h0);

    mel_clip_future_free(mel_clip_write_text(S8("present")));

    Mel_Future* hc = mel_clip_has();
    MEL_EXPECT(mel_clip_future_has(hc));
    mel_clip_future_free(hc);

    Mel_Future* hp = mel_clip_has(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_EXPECT(!mel_clip_future_has(hp));
    mel_clip_future_free(hp);

    mel_clip_future_free(mel_clip_write_text(S8("p"), .channel = MEL_CLIP_CHANNEL_PRIMARY));
    Mel_Future* hp2 = mel_clip_has(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_EXPECT(mel_clip_future_has(hp2));
    mel_clip_future_free(hp2);

    mel_clip_shutdown();
}

MEL_TEST(clipboard, channel_supported_reflects_backend)
{
    install_fake();
    MEL_EXPECT(mel_clip_channel_supported(MEL_CLIP_CHANNEL_CLIPBOARD));
    MEL_EXPECT(mel_clip_channel_supported(MEL_CLIP_CHANNEL_PRIMARY));
    mel_clip_shutdown();

    fake_primary_supported = false;
    mel_clip_init(mel_alloc_heap(), NULL);
    MEL_EXPECT(mel_clip_channel_supported(MEL_CLIP_CHANNEL_CLIPBOARD));
    MEL_EXPECT(!mel_clip_channel_supported(MEL_CLIP_CHANNEL_PRIMARY));
    mel_clip_shutdown();
    fake_primary_supported = true;
}

MEL_TEST(clipboard, unsupported_primary_channel_reads_no_clipboard)
{
    fake_primary_supported = false;
    mel_clip_init(mel_alloc_heap(), NULL);

    Mel_Future* r = mel_clip_read_text(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_REQUIRE(r != NULL);
    MEL_EXPECT(mel_clip_failed(mel_clip_future_status(r)));
    MEL_EXPECT((mel_clip_future_status(r) & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_future_free(r);

    mel_clip_shutdown();
    fake_primary_supported = true;
}

MEL_TEST(clipboard, unsupported_channel_rejected_before_dispatch_all_ops)
{
    fake_primary_supported = false;
    mel_clip_init(mel_alloc_heap(), NULL);

    mel_clip_future_free(mel_clip_write_text(S8("real clipboard")));
    u64 clip_seq = mel_clip_sequence_ch(MEL_CLIP_CHANNEL_CLIPBOARD);

    Mel_Future* w = mel_clip_write_text(S8("would clobber"), .channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_REQUIRE(w != NULL);
    MEL_EXPECT(mel_clip_failed(mel_clip_future_status(w)));
    MEL_EXPECT((mel_clip_future_status(w) & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_future_free(w);

    Mel_Future* c = mel_clip_clear(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_REQUIRE(c != NULL);
    MEL_EXPECT(mel_clip_failed(mel_clip_future_status(c)));
    MEL_EXPECT((mel_clip_future_status(c) & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_future_free(c);

    Mel_Future* q = mel_clip_query(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_REQUIRE(q != NULL);
    MEL_EXPECT(mel_clip_failed(mel_clip_future_status(q)));
    MEL_EXPECT((mel_clip_future_status(q) & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_future_free(q);

    Mel_Future* h = mel_clip_has(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_REQUIRE(h != NULL);
    MEL_EXPECT(mel_clip_failed(mel_clip_future_status(h)));
    MEL_EXPECT((mel_clip_future_status(h) & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_future_free(h);

    MEL_EXPECT_EQ(mel_clip_sequence_ch(MEL_CLIP_CHANNEL_CLIPBOARD), clip_seq);
    MEL_EXPECT_EQ(fake_prim.len, (usize)0);
    MEL_EXPECT_EQ(fake_prim.seq, (u64)0);

    Mel_Future* rc = mel_clip_read_text();
    MEL_EXPECT_EQ_STR8(mel_clip_future_text(rc), S8("real clipboard"));
    mel_clip_future_free(rc);

    mel_clip_shutdown();
    fake_primary_supported = true;
}

MEL_TEST(clipboard, sequence_is_channel_scoped)
{
    install_fake();
    u64 c0 = mel_clip_sequence_ch(MEL_CLIP_CHANNEL_CLIPBOARD);
    u64 p0 = mel_clip_sequence_ch(MEL_CLIP_CHANNEL_PRIMARY);

    mel_clip_future_free(mel_clip_write_text(S8("x"), .channel = MEL_CLIP_CHANNEL_PRIMARY));

    MEL_EXPECT_EQ(mel_clip_sequence_ch(MEL_CLIP_CHANNEL_CLIPBOARD), c0);
    MEL_EXPECT_GT(mel_clip_sequence_ch(MEL_CLIP_CHANNEL_PRIMARY), p0);
    MEL_EXPECT_EQ(mel_clip_sequence(), c0);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, watch_targets_requested_channel)
{
    install_fake();
    fake_prim.seq = 5;

    Mel_Event* ev = mel_clip_watch(.channel = MEL_CLIP_CHANNEL_PRIMARY);
    MEL_REQUIRE(ev != NULL);
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, NULL);

    u64 change = 6;
    mel_event_fire(ev, &change);
    u64 got = 0;
    MEL_EXPECT(mel_event_pull(ev, sub, &got));
    MEL_EXPECT_EQ(got, (u64)6);

    mel_event_unsubscribe(ev, sub);
    mel_clip_unwatch();
    mel_clip_shutdown();
}

static i64 leak_run(void (*body)(const Mel_Alloc* a), Counting_Alloc* cnt)
{
    Mel_Alloc tracked = { counting_cb, cnt };
    body(&tracked);
    return cnt->live;
}

static void body_success(const Mel_Alloc* a)
{
    fake_avail = true;
    fake_len = 0;
    fake_seq = 0;
    fake_defer_read = false;
    mel_clip_init(a, NULL);
    mel_clip_future_free(mel_clip_write_text(S8("count me")));
    Mel_Future* r = mel_clip_read_text();
    (void)mel_clip_future_text(r);
    mel_clip_future_free(r);
    Mel_Future* q = mel_clip_query();
    (void)mel_clip_future_formats(q);
    mel_clip_future_free(q);
    mel_clip_shutdown();
}

static void body_cancel(const Mel_Alloc* a)
{
    fake_avail = true;
    fake_len = 0;
    fake_seq = 0;
    fake_defer_read = true;
    mel_clip_init(a, NULL);
    Mel_Future* r = mel_clip_read_text();
    Cancel_Cont c = { 0 };
    c.fut = r;
    mel_task_init(&c.task, cancel_cont_run);
    mel_future_then(r, &c.task, mel_executor_inline());
    mel_clip_shutdown();
}

static void body_watch(const Mel_Alloc* a)
{
    fake_avail = true;
    fake_len = 0;
    fake_seq = 7;
    fake_defer_read = false;
    mel_clip_init(a, NULL);
    Mel_Event*    ev = mel_clip_watch();
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, NULL);
    u64           change = 8;
    mel_event_fire(ev, &change);
    u64 got = 0;
    (void)mel_event_pull(ev, sub, &got);
    mel_event_unsubscribe(ev, sub);
    mel_clip_shutdown();
}

static void body_shutdown_pending(const Mel_Alloc* a)
{
    fake_avail = true;
    fake_len = 0;
    fake_seq = 0;
    fake_defer_read = true;
    mel_clip_init(a, NULL);
    (void)mel_clip_read_text();
    (void)mel_clip_read_text();
    mel_clip_shutdown();
}

MEL_TEST(clipboard, no_leak_success_path)
{
    Counting_Alloc cnt = { 0, 0 };
    MEL_EXPECT_EQ(leak_run(body_success, &cnt), (i64)0);
}

MEL_TEST(clipboard, no_leak_cancel_path)
{
    Counting_Alloc cnt = { 0, 0 };
    MEL_EXPECT_EQ(leak_run(body_cancel, &cnt), (i64)0);
}

MEL_TEST(clipboard, no_leak_shutdown_pending_path)
{
    Counting_Alloc cnt = { 0, 0 };
    MEL_EXPECT_EQ(leak_run(body_shutdown_pending, &cnt), (i64)0);
}

MEL_TEST(clipboard, no_leak_watch_path)
{
    Counting_Alloc cnt = { 0, 0 };
    MEL_EXPECT_EQ(leak_run(body_watch, &cnt), (i64)0);
}
