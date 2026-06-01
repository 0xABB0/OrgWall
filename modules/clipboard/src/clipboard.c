#include <clipboard/clipboard.h>
#include <clipboard/backend.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <collection.slotmap/slotmap.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <string.h>

#define MEL_CLIP_WATCH_POLL_NS (250ll * 1000ll * 1000ll)

struct Mel_Clip_Job
{
    Mel_SlotMap_Handle self;
    Mel_Reactor*       reactor;
    const Mel_Alloc*   alloc;
    void*              user;

    Mel_Clip_On_Read  on_read;
    Mel_Clip_On_Text  on_text;
    Mel_Clip_On_Write on_write;
    Mel_Clip_On_Query on_query;

    Mel_Array(Mel_Clip_Format) request;
    Mel_Clip_Transferable payload;

    Mel_Clip_Transferable result;
    Mel_Array(Mel_Clip_Format) result_formats;

    Mel_Clip_Status     status;
    bool                resolved;
    Mel_Reactor_Source* deliver_timer;
};

typedef struct
{
    str8 mime;
    bool owned;
} Format_Entry;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;

    Mel_SlotMap jobs;
    Mel_Array(Format_Entry) formats;

    Mel_Reactor_Source* watch_timer;
    Mel_Clip_On_Change  watch_cb;
    void*               watch_user;
    Mel_Reactor*        watch_reactor;
    u64                 watch_last_seq;
} Clip;

static Clip g;

static str8 dup_bytes(const Mel_Alloc* a, const void* p, usize n)
{
    if (n == 0 || !p)
        return STR8_EMPTY;
    u8* d = (u8*)mel_alloc(a, n);
    if (!d)
        return STR8_EMPTY;
    memcpy(d, p, n);
    return (str8){ d, (size)n };
}

void mel_clip_transferable_init(Mel_Clip_Transferable* t, const Mel_Alloc* alloc)
{
    const Mel_Alloc* a = alloc ? alloc : mel_alloc_heap();
    t->alloc = a;
    mel_array_init(&t->items, a);
}

Mel_Clip_Item* mel_clip_item_add(Mel_Clip_Transferable* t)
{
    Mel_Clip_Item it;
    mel_array_init(&it.reps, t->alloc);
    mel_array_push(&t->items, it);
    return &mel_array_last(&t->items);
}

void mel_clip_rep_add(Mel_Clip_Item* it, Mel_Clip_Format f, str8 bytes, const Mel_Alloc* alloc)
{
    Mel_Clip_Rep r = { .format = f, .bytes = dup_bytes(alloc, bytes.data, (usize)bytes.len) };
    mel_array_push(&it->reps, r);
}

void mel_clip_transferable_free(Mel_Clip_Transferable* t)
{
    for (usize i = 0; i < t->items.count; i++)
    {
        Mel_Clip_Item* it = &t->items.items[i];
        for (usize j = 0; j < it->reps.count; j++)
            if (it->reps.items[j].bytes.data)
                mel_dealloc(it->reps.allocator, it->reps.items[j].bytes.data);
        mel_array_free(&it->reps);
    }
    mel_array_free(&t->items);
}

static Mel_Clip_Format format_intern(str8 mime, bool dup)
{
    for (usize i = 0; i < g.formats.count; i++)
        if (str8_equals(g.formats.items[i].mime, mime))
            return (Mel_Clip_Format)(i + 1);
    Format_Entry e = { .mime = dup ? str8_dup(mime, g.alloc) : mime, .owned = dup };
    mel_array_push(&g.formats, e);
    return (Mel_Clip_Format)g.formats.count;
}

static void seed_formats(void)
{
    format_intern(S8("text/plain;charset=utf-8"), false);
    format_intern(S8("text/html"), false);
    format_intern(S8("image/png"), false);
    format_intern(S8("text/uri-list"), false);
    format_intern(S8("text/rtf"), false);
}

Mel_Clip_Format mel_clip_format_register(str8 mime)
{
    if (!g.initialized || str8_is_empty(mime))
        return MEL_CLIP_FMT_NONE;
    return format_intern(mime, true);
}

str8 mel_clip_format_mime(Mel_Clip_Format f)
{
    if (!g.initialized || f == MEL_CLIP_FMT_NONE || f > g.formats.count)
        return STR8_EMPTY;
    return g.formats.items[f - 1].mime;
}

void mel_clip_init(const Mel_Alloc* alloc, Mel_Reactor* reactor)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.reactor = reactor;
    mel_slotmap_init(&g.jobs, g.alloc, .item_size = sizeof(Mel_Clip_Job*), .initial_capacity = 8);
    mel_array_init(&g.formats, g.alloc);
    seed_formats();
    g.initialized = true;
}

static void job_free(Mel_Clip_Job* j)
{
    mel_array_free(&j->request);
    mel_clip_transferable_free(&j->payload);
    mel_clip_transferable_free(&j->result);
    mel_array_free(&j->result_formats);
    mel_slotmap_remove(&g.jobs, j->self);
    mel_dealloc(g.alloc, j);
}

static void deliver_inline(Mel_Clip_Job* j)
{
    if (j->on_read)
        j->on_read(&j->result, j->status, j->user);
    else if (j->on_text)
    {
        str8 text = STR8_EMPTY;
        if (j->result.items.count > 0)
        {
            Mel_Clip_Item* it = &j->result.items.items[0];
            for (usize i = 0; i < it->reps.count; i++)
                if (it->reps.items[i].format == MEL_CLIP_FMT_TEXT)
                {
                    text = it->reps.items[i].bytes;
                    break;
                }
        }
        j->on_text(text, j->status, j->user);
    }
    else if (j->on_query)
        j->on_query(j->result_formats.items, (u32)j->result_formats.count, j->status, j->user);
    else if (j->on_write)
        j->on_write(j->status, j->user);
    job_free(j);
}

static bool deliver_timer_cb(void* user)
{
    Mel_Clip_Job* j = (Mel_Clip_Job*)user;
    j->deliver_timer = NULL;
    deliver_inline(j);
    return false;
}

void mel_clip_job_resolve(Mel_Clip_Job* j, Mel_Clip_Status s)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->status |= s;
    if (j->reactor)
    {
        j->deliver_timer = mel_reactor_timer_new(0, deliver_timer_cb, j);
        if (j->deliver_timer)
        {
            mel_reactor_source_attach(j->reactor, j->deliver_timer);
            return;
        }
    }
    deliver_inline(j);
}

static Mel_Clip_Job* job_new(Mel_Clip_Opt opt)
{
    Mel_Clip_Job* j = mel_alloc_type(g.alloc, Mel_Clip_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->reactor = opt.reactor ? opt.reactor : g.reactor;
    j->alloc = opt.alloc ? opt.alloc : g.alloc;
    j->user = opt.user;
    mel_array_init(&j->request, g.alloc);
    mel_clip_transferable_init(&j->payload, g.alloc);
    mel_clip_transferable_init(&j->result, j->alloc);
    mel_array_init(&j->result_formats, j->alloc);
    Mel_Clip_Job* slot = j;
    j->self = mel_slotmap_insert(&g.jobs, &slot);
    return j;
}

static bool backend_ready(void) { return g.initialized && mel_clip__plat_available(); }

static void copy_request(Mel_Clip_Job* j, const Mel_Clip_Format* fmts, u32 n)
{
    for (u32 i = 0; i < n; i++)
        mel_array_push(&j->request, fmts[i]);
}

static void copy_payload(Mel_Clip_Job* j, const Mel_Clip_Transferable* t)
{
    for (usize i = 0; i < t->items.count; i++)
    {
        Mel_Clip_Item* src = &t->items.items[i];
        Mel_Clip_Item* dst = mel_clip_item_add(&j->payload);
        for (usize r = 0; r < src->reps.count; r++)
            mel_clip_rep_add(dst, src->reps.items[r].format, src->reps.items[r].bytes, j->payload.alloc);
    }
}

void mel_clip_read_opt(const Mel_Clip_Format* fmts, u32 n, Mel_Clip_On_Read cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    Mel_Clip_Job* j = job_new(opt);
    if (!j)
        return;
    j->on_read = cb;
    copy_request(j, fmts, n);
    if (!backend_ready())
    {
        mel_log_error("clipboard", "read: no clipboard backend");
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    mel_clip__plat_read(j);
}

void mel_clip_read_text_opt(Mel_Clip_On_Text cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    Mel_Clip_Job* j = job_new(opt);
    if (!j)
        return;
    j->on_text = cb;
    mel_array_push(&j->request, (Mel_Clip_Format)MEL_CLIP_FMT_TEXT);
    if (!backend_ready())
    {
        mel_log_error("clipboard", "read_text: no clipboard backend");
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    mel_clip__plat_read(j);
}

void mel_clip_write_opt(const Mel_Clip_Transferable* t, Mel_Clip_On_Write cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    Mel_Clip_Job* j = job_new(opt);
    if (!j)
        return;
    j->on_write = cb;
    if (t)
        copy_payload(j, t);
    if (!backend_ready())
    {
        mel_log_error("clipboard", "write: no clipboard backend");
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    mel_clip__plat_write(j);
}

void mel_clip_write_text_opt(str8 text, Mel_Clip_On_Write cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    Mel_Clip_Job* j = job_new(opt);
    if (!j)
        return;
    j->on_write = cb;
    Mel_Clip_Item* it = mel_clip_item_add(&j->payload);
    mel_clip_rep_add(it, MEL_CLIP_FMT_TEXT, text, j->payload.alloc);
    if (!backend_ready())
    {
        mel_log_error("clipboard", "write_text: no clipboard backend");
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    mel_clip__plat_write(j);
}

void mel_clip_query_opt(Mel_Clip_On_Query cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    Mel_Clip_Job* j = job_new(opt);
    if (!j)
        return;
    j->on_query = cb;
    if (!backend_ready())
    {
        mel_log_error("clipboard", "query: no clipboard backend");
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    mel_clip__plat_query(j);
}

void mel_clip_clear_opt(Mel_Clip_On_Write cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    Mel_Clip_Job* j = job_new(opt);
    if (!j)
        return;
    j->on_write = cb;
    if (!backend_ready())
    {
        mel_log_error("clipboard", "clear: no clipboard backend");
        mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
        return;
    }
    mel_clip__plat_clear(j);
}

bool mel_clip_available(void) { return backend_ready(); }

u64 mel_clip_sequence(void) { return g.initialized ? mel_clip__plat_sequence() : 0; }

void* mel_clip_native(void) { return g.initialized ? mel_clip__plat_native() : NULL; }

static bool watch_tick(void* user)
{
    (void)user;
    u64 s = mel_clip_sequence();
    if (s != g.watch_last_seq)
    {
        g.watch_last_seq = s;
        if (g.watch_cb)
            g.watch_cb(s, g.watch_user);
    }
    return true;
}

void mel_clip_watch_opt(Mel_Clip_On_Change cb, Mel_Clip_Opt opt)
{
    if (!g.initialized)
        return;
    if (mel_clip__plat_sequence() == 0)
    {
        mel_log_warn("clipboard", "watch unsupported on this backend");
        return;
    }
    mel_clip_unwatch();
    g.watch_cb = cb;
    g.watch_user = opt.user;
    g.watch_reactor = opt.reactor ? opt.reactor : g.reactor;
    g.watch_last_seq = mel_clip__plat_sequence();
    if (!g.watch_reactor)
        return;
    g.watch_timer = mel_reactor_timer_new(MEL_CLIP_WATCH_POLL_NS, watch_tick, NULL);
    if (g.watch_timer)
        mel_reactor_source_attach(g.watch_reactor, g.watch_timer);
}

void mel_clip_unwatch(void)
{
    if (g.watch_timer)
    {
        mel_reactor_source_destroy(g.watch_timer);
        g.watch_timer = NULL;
    }
    g.watch_cb = NULL;
    g.watch_user = NULL;
}

void mel_clip_shutdown(void)
{
    if (!g.initialized)
        return;
    mel_clip_unwatch();

    Mel_Array(Mel_Clip_Job*) snap;
    mel_array_init(&snap, g.alloc);
    Mel_Clip_Job** data = (Mel_Clip_Job**)mel_slotmap_data(&g.jobs);
    u32            n = mel_slotmap_count(&g.jobs);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
    {
        Mel_Clip_Job* j = snap.items[i];
        if (j->deliver_timer)
        {
            mel_reactor_source_destroy(j->deliver_timer);
            j->deliver_timer = NULL;
        }
        if (!j->resolved)
            j->status |= MEL_CLIP_ERROR | MEL_CLIP_RESULT_CANCELLED;
        deliver_inline(j);
    }
    mel_array_free(&snap);

    for (usize i = 0; i < g.formats.count; i++)
        if (g.formats.items[i].owned && g.formats.items[i].mime.data)
            mel_dealloc(g.alloc, g.formats.items[i].mime.data);
    mel_array_free(&g.formats);
    mel_slotmap_free(&g.jobs);
    memset(&g, 0, sizeof g);
}

const Mel_Alloc* mel_clip_job_alloc(const Mel_Clip_Job* j) { return j ? j->alloc : NULL; }

u64 mel_clip_job_token(const Mel_Clip_Job* j) { return j ? mel_slotmap_handle_pack64(j->self) : 0; }

Mel_Clip_Job* mel_clip__job_from_token(u64 token)
{
    if (!g.initialized)
        return NULL;
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64(token);
    Mel_Clip_Job**     pp = (Mel_Clip_Job**)mel_slotmap_get(&g.jobs, h);
    return pp ? *pp : NULL;
}

u32 mel_clip_job_request_count(const Mel_Clip_Job* j) { return j ? (u32)j->request.count : 0; }

Mel_Clip_Format mel_clip_job_request(const Mel_Clip_Job* j, u32 i) { return (j && i < j->request.count) ? j->request.items[i] : MEL_CLIP_FMT_NONE; }

bool mel_clip_job_wants(const Mel_Clip_Job* j, Mel_Clip_Format f)
{
    if (!j || j->request.count == 0)
        return true;
    for (usize i = 0; i < j->request.count; i++)
        if (j->request.items[i] == f)
            return true;
    return false;
}

u32 mel_clip_job_item_count(const Mel_Clip_Job* j) { return j ? (u32)j->payload.items.count : 0; }

u32 mel_clip_job_rep_count(const Mel_Clip_Job* j, u32 item) { return (j && item < j->payload.items.count) ? (u32)j->payload.items.items[item].reps.count : 0; }

Mel_Clip_Rep mel_clip_job_rep(const Mel_Clip_Job* j, u32 item, u32 rep)
{
    if (j && item < j->payload.items.count && rep < j->payload.items.items[item].reps.count)
        return j->payload.items.items[item].reps.items[rep];
    return (Mel_Clip_Rep){ .format = MEL_CLIP_FMT_NONE, .bytes = STR8_EMPTY };
}

void mel_clip_job_emit(Mel_Clip_Job* j, Mel_Clip_Format f, const void* bytes, usize len)
{
    if (!j)
        return;
    Mel_Clip_Item* it = j->result.items.count > 0 ? &j->result.items.items[0] : mel_clip_item_add(&j->result);
    Mel_Clip_Rep   r = { .format = f, .bytes = dup_bytes(j->alloc, bytes, len) };
    mel_array_push(&it->reps, r);
}

void mel_clip_job_emit_format(Mel_Clip_Job* j, Mel_Clip_Format f)
{
    if (j)
        mel_array_push(&j->result_formats, f);
}

void mel_clip_job_add_warning(Mel_Clip_Job* j, Mel_Clip_Status warn_bits)
{
    if (j)
        j->status |= warn_bits;
}
