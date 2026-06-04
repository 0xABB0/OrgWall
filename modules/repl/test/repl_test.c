#include <test/test.h>

#include <repl/repl.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>

#include <string.h>

static int g_lang_destroyed = 0;

static Mel_Repl_Result mock_eval(void* self, str8 line, const Mel_Alloc* a)
{
    (void)self;
    (void)line;
    Mel_Repl_Result r;
    r.ok = true;
    r.diagnostics = NULL;
    r.alloc = a;
    char* t = (char*)mel_alloc(a, 3);
    memcpy(t, "ok", 3);
    r.text = t;
    return r;
}

static void mock_destroy(void* self)
{
    (void)self;
    g_lang_destroyed = 1;
}

MEL_TEST(repl, loop_dispatches_to_lang)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Repl_Lang    lang = { 0 };
    lang.eval = mock_eval;
    lang.destroy = mock_destroy;

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Result r = mel_repl_eval(repl, S8("anything"));
    MEL_EXPECT(r.ok);
    MEL_EXPECT_STR_EQ(r.text, "ok");
    mel_repl_result_free(&r);
    MEL_EXPECT_NULL(r.text);

    g_lang_destroyed = 0;
    mel_repl_destroy(repl);
    MEL_EXPECT_EQ(g_lang_destroyed, 1);
}

MEL_TEST(repl, create_rejects_null_lang)
{
    Mel_Repl_Lang empty = { 0 };
    MEL_EXPECT_NULL(mel_repl_create(mel_alloc_heap(), empty));
}

typedef struct
{
    const char** lines;
    usize        count;
    usize        cursor;
} Script_Source;

static bool script_read(void* self, str8* out)
{
    Script_Source* s = (Script_Source*)self;
    if (s->cursor >= s->count)
        return false;
    *out = str8_from_cstr(s->lines[s->cursor]);
    s->cursor++;
    return true;
}

typedef struct
{
    Mel_Array(u8) buf;
} Capture_Sink;

static void capture_write(void* self, str8 bytes)
{
    Capture_Sink* c = (Capture_Sink*)self;
    for (size i = 0; i < bytes.len; i++)
        mel_array_push(&c->buf, bytes.data[i]);
}

typedef struct
{
    int  eval_calls;
    bool needs_continuation;
} Echo_Lang;

static Mel_Repl_Result echo_eval(void* self, str8 line, const Mel_Alloc* a)
{
    Echo_Lang* l = (Echo_Lang*)self;
    l->eval_calls++;

    Mel_Repl_Result r;
    r.ok = true;
    r.diagnostics = NULL;
    r.alloc = a;

    str8  marked = str8_fmt_alloc(a, "=%.*s", (int)line.len, (const char*)line.data);
    char* t = (char*)mel_alloc(a, (usize)marked.len + 1);
    memcpy(t, marked.data, (usize)marked.len);
    t[marked.len] = '\0';
    mel_dealloc(a, marked.data);
    r.text = t;
    return r;
}

static bool echo_complete(void* self, str8 accumulated)
{
    Echo_Lang* l = (Echo_Lang*)self;
    if (!l->needs_continuation)
        return true;
    return str8_ends_with(accumulated, S8(";"));
}

MEL_TEST(repl, run_prompts_echoes_and_dispatches)
{
    const Mel_Alloc* a = mel_alloc_heap();

    const char*   lines[] = { "one", "two" };
    Script_Source src = { lines, 2, 0 };

    Capture_Sink cap;
    mel_array_init(&cap.buf, a);

    Echo_Lang     el = { 0, false };
    Mel_Repl_Lang lang = { 0 };
    lang.self = &el;
    lang.eval = echo_eval;
    lang.complete = echo_complete;

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Source  source = { &src, script_read, NULL };
    Mel_Repl_Sink    sink = { &cap, capture_write, NULL };
    Mel_Repl_Prompts prompts = { S8("> "), S8(".. ") };

    usize n = mel_repl_run(repl, source, sink, prompts);
    MEL_EXPECT_EQ(n, (usize)2);
    MEL_EXPECT_EQ(el.eval_calls, 2);

    str8 out = str8_from_parts(cap.buf.items, (size)cap.buf.count);
    MEL_EXPECT_EQ_STR8(out, S8("> one\n=one\n> two\n=two\n> "));

    mel_array_free(&cap.buf);
    mel_repl_destroy(repl);
}

MEL_TEST(repl, run_records_history)
{
    const Mel_Alloc* a = mel_alloc_heap();

    const char*   lines[] = { "alpha", "beta" };
    Script_Source src = { lines, 2, 0 };

    Capture_Sink cap;
    mel_array_init(&cap.buf, a);

    Echo_Lang     el = { 0, false };
    Mel_Repl_Lang lang = { 0 };
    lang.self = &el;
    lang.eval = echo_eval;
    lang.complete = echo_complete;

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Source  source = { &src, script_read, NULL };
    Mel_Repl_Sink    sink = { &cap, capture_write, NULL };
    Mel_Repl_Prompts prompts = { S8("> "), S8(".. ") };

    mel_repl_run(repl, source, sink, prompts);

    usize       hc;
    const str8* hist = mel_repl_history(repl, &hc);
    MEL_REQUIRE_EQ(hc, (usize)2);
    MEL_EXPECT_EQ_STR8(hist[0], S8("alpha"));
    MEL_EXPECT_EQ_STR8(hist[1], S8("beta"));

    mel_array_free(&cap.buf);
    mel_repl_destroy(repl);
}

MEL_TEST(repl, run_continues_until_terminated)
{
    const Mel_Alloc* a = mel_alloc_heap();

    const char*   lines[] = { "int x", "= 5;", "done;" };
    Script_Source src = { lines, 3, 0 };

    Capture_Sink cap;
    mel_array_init(&cap.buf, a);

    Echo_Lang     el = { 0, true };
    Mel_Repl_Lang lang = { 0 };
    lang.self = &el;
    lang.eval = echo_eval;
    lang.complete = echo_complete;

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Source  source = { &src, script_read, NULL };
    Mel_Repl_Sink    sink = { &cap, capture_write, NULL };
    Mel_Repl_Prompts prompts = { S8("> "), S8(".. ") };

    usize n = mel_repl_run(repl, source, sink, prompts);
    MEL_EXPECT_EQ(n, (usize)2);
    MEL_EXPECT_EQ(el.eval_calls, 2);

    usize       hc;
    const str8* hist = mel_repl_history(repl, &hc);
    MEL_REQUIRE_EQ(hc, (usize)2);
    MEL_EXPECT_EQ_STR8(hist[0], S8("int x\n= 5;"));
    MEL_EXPECT_EQ_STR8(hist[1], S8("done;"));

    str8 out = str8_from_parts(cap.buf.items, (size)cap.buf.count);
    MEL_EXPECT_EQ_STR8(out, S8("> .. int x\n= 5;\n=int x\n= 5;\n> done;\n=done;\n> "));

    mel_array_free(&cap.buf);
    mel_repl_destroy(repl);
}

MEL_TEST(repl, run_rejects_null_source)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Repl_Lang    lang = { 0 };
    Echo_Lang        el = { 0, false };
    lang.self = &el;
    lang.eval = echo_eval;

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Capture_Sink cap;
    mel_array_init(&cap.buf, a);

    Mel_Repl_Source  bad = { NULL, NULL, NULL };
    Mel_Repl_Sink    sink = { &cap, capture_write, NULL };
    Mel_Repl_Prompts prompts = { S8("> "), S8(".. ") };

    usize n = mel_repl_run(repl, bad, sink, prompts);
    MEL_EXPECT_EQ(n, (usize)0);
    MEL_EXPECT_EQ(el.eval_calls, 0);

    mel_array_free(&cap.buf);
    mel_repl_destroy(repl);
}

MEL_TEST(repl, run_dispatches_unterminated_at_eof)
{
    const Mel_Alloc* a = mel_alloc_heap();

    const char*   lines[] = { "int x", "= 5" };
    Script_Source src = { lines, 2, 0 };

    Capture_Sink cap;
    mel_array_init(&cap.buf, a);

    Echo_Lang     el = { 0, true };
    Mel_Repl_Lang lang = { 0 };
    lang.self = &el;
    lang.eval = echo_eval;
    lang.complete = echo_complete;

    Mel_Repl* repl = mel_repl_create(a, lang);
    MEL_REQUIRE_NOT_NULL(repl);

    Mel_Repl_Source  source = { &src, script_read, NULL };
    Mel_Repl_Sink    sink = { &cap, capture_write, NULL };
    Mel_Repl_Prompts prompts = { S8("> "), S8(".. ") };

    usize n = mel_repl_run(repl, source, sink, prompts);
    MEL_EXPECT_EQ(n, (usize)1);
    MEL_EXPECT_EQ(el.eval_calls, 1);

    usize       hc;
    const str8* hist = mel_repl_history(repl, &hc);
    MEL_REQUIRE_EQ(hc, (usize)1);
    MEL_EXPECT_EQ_STR8(hist[0], S8("int x\n= 5"));

    mel_array_free(&cap.buf);
    mel_repl_destroy(repl);
}
