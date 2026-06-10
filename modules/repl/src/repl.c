#include <repl/repl.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <log/log.h>
#include <string/str8.h>

struct Mel_Repl
{
    const Mel_Alloc* allocator;
    Mel_Repl_Lang    lang;
    Mel_Array(str8) history;
};

Mel_Repl* mel_repl_create(const Mel_Alloc* allocator, Mel_Repl_Lang lang)
{
    if (!allocator)
    {
        mel_log_error("repl", "mel_repl_create requires a non-null allocator");
        return NULL;
    }
    if (!lang.eval)
    {
        mel_log_error("repl", "mel_repl_create requires a language with an eval");
        return NULL;
    }

    Mel_Repl* repl = mel_alloc_type(allocator, Mel_Repl);
    if (!repl)
        return NULL;

    repl->allocator = allocator;
    repl->lang = lang;
    mel_array_init(&repl->history, allocator);
    return repl;
}

void mel_repl_destroy(Mel_Repl* repl)
{
    if (!repl)
        return;
    for (usize i = 0; i < repl->history.count; i++)
        mel_dealloc(repl->allocator, repl->history.items[i].data);
    mel_array_free(&repl->history);
    if (repl->lang.destroy)
        repl->lang.destroy(repl->lang.self);
    mel_dealloc(repl->allocator, repl);
}

static void mel_repl_record(Mel_Repl* repl, str8 input)
{
    str8 owned = str8_dup_alloc(input, repl->allocator);
    mel_array_push(&repl->history, owned);
}

Mel_Repl_Result mel_repl_eval(Mel_Repl* repl, str8 line)
{
    mel_repl_record(repl, line);
    return repl->lang.eval(repl->lang.self, line, repl->allocator);
}

void mel_repl_result_free(Mel_Repl_Result* r)
{
    if (!r)
        return;
    if (r->alloc)
    {
        if (r->text)
            mel_dealloc(r->alloc, r->text);
        if (r->diagnostics)
            mel_dealloc(r->alloc, r->diagnostics);
    }
    r->text = NULL;
    r->diagnostics = NULL;
    r->alloc = NULL;
}

const str8* mel_repl_history(const Mel_Repl* repl, usize* count)
{
    if (!repl || !count)
    {
        mel_log_error("repl", "mel_repl_history requires a non-null repl and count");
        if (count)
            *count = 0;
        return NULL;
    }
    *count = repl->history.count;
    return repl->history.items;
}

static void mel_repl_emit(Mel_Repl_Sink sink, str8 bytes)
{
    if (bytes.len > 0)
        sink.write(sink.self, bytes);
}

struct Mel_Repl_Drive
{
    Mel_Repl*        repl;
    Mel_Repl_Sink    sink;
    Mel_Repl_Prompts prompts;
    Mel_Array(u8) unit;
    bool  first;
    usize evaluated;
};

Mel_Repl_Drive* mel_repl_drive_create(Mel_Repl* repl, Mel_Repl_Sink sink, Mel_Repl_Prompts prompts)
{
    if (!repl)
    {
        mel_log_error("repl", "mel_repl_drive_create requires a non-null repl");
        return NULL;
    }
    if (!sink.write)
    {
        mel_log_error("repl", "mel_repl_drive_create requires a sink with a write callback");
        return NULL;
    }

    Mel_Repl_Drive* d = mel_alloc_type(repl->allocator, Mel_Repl_Drive);
    if (!d)
        return NULL;

    d->repl = repl;
    d->sink = sink;
    d->prompts = prompts;
    mel_array_init(&d->unit, repl->allocator);
    d->first = true;
    d->evaluated = 0;

    mel_repl_emit(d->sink, d->prompts.primary);
    return d;
}

static void mel_repl_drive_dispatch(Mel_Repl_Drive* d)
{
    str8 accumulated = str8_from_parts(d->unit.items, (size)d->unit.count);

    mel_repl_emit(d->sink, accumulated);
    mel_repl_emit(d->sink, S8("\n"));

    Mel_Repl_Result r = mel_repl_eval(d->repl, accumulated);
    d->evaluated++;

    if (r.ok)
    {
        if (r.text)
            mel_repl_emit(d->sink, str8_from_cstr(r.text));
    }
    else if (r.diagnostics)
    {
        mel_repl_emit(d->sink, str8_from_cstr(r.diagnostics));
    }
    mel_repl_emit(d->sink, S8("\n"));

    mel_repl_result_free(&r);

    mel_array_clear(&d->unit);
    d->first = true;
}

void mel_repl_drive_line(Mel_Repl_Drive* d, str8 line)
{
    if (!d)
        return;

    if (!d->first)
        mel_array_push(&d->unit, (u8)'\n');
    for (size i = 0; i < line.len; i++)
        mel_array_push(&d->unit, line.data[i]);
    d->first = false;

    str8 accumulated = str8_from_parts(d->unit.items, (size)d->unit.count);
    if (!d->repl->lang.complete || d->repl->lang.complete(d->repl->lang.self, accumulated))
    {
        mel_repl_drive_dispatch(d);
        mel_repl_emit(d->sink, d->prompts.primary);
    }
    else
    {
        mel_repl_emit(d->sink, d->prompts.continuation);
    }
}

usize mel_repl_drive_destroy(Mel_Repl_Drive* d)
{
    if (!d)
        return 0;

    if (!d->first)
    {
        mel_log_warn("repl", "input ended mid-unit; dispatching the unterminated fragment");
        mel_repl_drive_dispatch(d);
    }

    usize            evaluated = d->evaluated;
    const Mel_Alloc* a = d->repl->allocator;
    mel_array_free(&d->unit);
    mel_dealloc(a, d);
    return evaluated;
}

usize mel_repl_run(Mel_Repl* repl, Mel_Repl_Source source, Mel_Repl_Sink sink, Mel_Repl_Prompts prompts)
{
    if (!repl)
    {
        mel_log_error("repl", "mel_repl_run requires a non-null repl");
        return 0;
    }
    if (!source.read)
    {
        mel_log_error("repl", "mel_repl_run requires a source with a read callback");
        return 0;
    }
    if (!sink.write)
    {
        mel_log_error("repl", "mel_repl_run requires a sink with a write callback");
        return 0;
    }

    Mel_Repl_Drive* d = mel_repl_drive_create(repl, sink, prompts);
    if (!d)
        return 0;

    str8 line;
    while (source.read(source.self, &line))
        mel_repl_drive_line(d, line);

    return mel_repl_drive_destroy(d);
}
