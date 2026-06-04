#include "clip_linux.h"

#include <allocator/allocator.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <dlfcn.h>
#include <string.h>

typedef struct wl_display wl_display;

typedef struct
{
    wl_display* (*display_connect)(const char*);
    void (*display_disconnect)(wl_display*);
    int (*display_get_fd)(wl_display*);
    int (*display_dispatch)(wl_display*);
    int (*display_dispatch_pending)(wl_display*);
    int (*display_flush)(wl_display*);
    int (*display_roundtrip)(wl_display*);
} Wl_Api;

typedef struct
{
    void*               lib;
    Wl_Api              api;
    wl_display*         display;
    Mel_Reactor_Source* source;
    Mel_Reactor_Poll    poll;
    str8                clip_bytes;
    str8                prim_bytes;
    bool                clip_valid;
    bool                prim_valid;
    u64                 clip_seq;
    u64                 prim_seq;
    bool                ok;
} Wl_State;

static Wl_State g_wl;

static void* wl_sym(void* lib, const char* name)
{
    void* s = dlsym(lib, name);
    if (!s)
        mel_log_error("clipboard", "wayland backend: symbol '%s' missing from libwayland-client", name);
    return s;
}

static bool wl_load(Wl_State* w)
{
    w->lib = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!w->lib)
        w->lib = dlopen("libwayland-client.so", RTLD_NOW | RTLD_GLOBAL);
    if (!w->lib)
        return false;
    Wl_Api* a = &w->api;
    a->display_connect = (wl_display * (*)(const char*)) wl_sym(w->lib, "wl_display_connect");
    a->display_disconnect = (void (*)(wl_display*))wl_sym(w->lib, "wl_display_disconnect");
    a->display_get_fd = (int (*)(wl_display*))wl_sym(w->lib, "wl_display_get_fd");
    a->display_dispatch = (int (*)(wl_display*))wl_sym(w->lib, "wl_display_dispatch");
    a->display_dispatch_pending = (int (*)(wl_display*))wl_sym(w->lib, "wl_display_dispatch_pending");
    a->display_flush = (int (*)(wl_display*))wl_sym(w->lib, "wl_display_flush");
    a->display_roundtrip = (int (*)(wl_display*))wl_sym(w->lib, "wl_display_roundtrip");
    return a->display_connect && a->display_disconnect && a->display_get_fd && a->display_dispatch && a->display_dispatch_pending && a->display_flush && a->display_roundtrip;
}

static bool wl_source_prepare(Mel_Reactor_Source* source, i32* timeout)
{
    (void)source;
    *timeout = MEL_REACTOR_FOREVER;
    if (g_wl.display)
        g_wl.api.display_flush(g_wl.display);
    return false;
}

static bool wl_source_check(Mel_Reactor_Source* source)
{
    if (source->poll_count == 0 || !source->polls[0])
        return false;
    return (source->polls[0]->revents & (MEL_REACTOR_POLL_IN | MEL_REACTOR_POLL_HUP | MEL_REACTOR_POLL_ERR)) != 0;
}

static bool wl_source_dispatch(Mel_Reactor_Source* source, Mel_Reactor_Source_Proc cb, void* user)
{
    (void)source;
    (void)cb;
    (void)user;
    if (g_wl.api.display_dispatch(g_wl.display) < 0)
    {
        mel_log_error("clipboard", "wayland backend: display dispatch failed");
        return false;
    }
    g_wl.api.display_flush(g_wl.display);
    return true;
}

static const Mel_Reactor_Source_Callbacks g_wl_cb = {
    .prepare = wl_source_prepare,
    .check = wl_source_check,
    .dispatch = wl_source_dispatch,
    .finalize = NULL,
};

bool mel_clip__wl_init(void)
{
    Wl_State* w = &g_wl;
    if (w->ok)
        return true;
    if (!wl_load(w))
    {
        if (w->lib)
        {
            dlclose(w->lib);
            w->lib = NULL;
        }
        return false;
    }
    w->display = w->api.display_connect(NULL);
    if (!w->display)
    {
        dlclose(w->lib);
        memset(w, 0, sizeof *w);
        return false;
    }
    w->api.display_roundtrip(w->display);

    Mel_Reactor* reactor = mel_clip__reactor();
    if (reactor)
    {
        w->source = mel_reactor_source_new(&g_wl_cb, sizeof(Mel_Reactor_Source));
        w->poll = (Mel_Reactor_Poll){ .handle = w->api.display_get_fd(w->display), .events = MEL_REACTOR_POLL_IN };
        mel_reactor_source_add_poll(w->source, &w->poll);
        mel_reactor_source_set_priority(w->source, MEL_REACTOR_PRIORITY_HIGH);
        mel_reactor_source_attach(reactor, w->source);
    }
    w->ok = true;
    return true;
}

void mel_clip__wl_shutdown(void)
{
    Wl_State* w = &g_wl;
    if (!w->ok)
        return;
    if (w->source)
    {
        mel_reactor_source_destroy(w->source);
        w->source = NULL;
    }
    const Mel_Alloc* al = mel_clip__alloc();
    if (w->clip_bytes.data && al)
        mel_dealloc(al, w->clip_bytes.data);
    if (w->prim_bytes.data && al)
        mel_dealloc(al, w->prim_bytes.data);
    if (w->display)
        w->api.display_disconnect(w->display);
    if (w->lib)
        dlclose(w->lib);
    memset(w, 0, sizeof *w);
}

u64 mel_clip__wl_sequence(Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY ? g_wl.prim_seq : g_wl.clip_seq; }

static str8* wl_owned(Wl_State* w, Mel_Clip_Channel ch, bool** valid)
{
    if (mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY)
    {
        *valid = &w->prim_valid;
        return &w->prim_bytes;
    }
    *valid = &w->clip_valid;
    return &w->clip_bytes;
}

void mel_clip__wl_read(Mel_Clip_Job* job)
{
    Wl_State* w = &g_wl;
    bool*     valid = NULL;
    str8*     own = wl_owned(w, mel_clip_job_channel(job), &valid);
    if (*valid && own->len > 0)
    {
        mel_clip_job_emit(job, MEL_CLIP_FMT_TEXT, own->data, (usize)own->len);
        mel_clip_job_resolve(job, MEL_CLIP_OK);
        return;
    }
    mel_clip_job_resolve(job, MEL_CLIP_RESULT_EMPTY);
}

void mel_clip__wl_write(Mel_Clip_Job* job)
{
    Wl_State* w = &g_wl;
    bool*     valid = NULL;
    str8*     own = wl_owned(w, mel_clip_job_channel(job), &valid);

    str8 text = STR8_EMPTY;
    u32  reps = mel_clip_job_rep_count(job, 0);
    for (u32 r = 0; r < reps; r++)
    {
        Mel_Clip_Rep rep = mel_clip_job_rep(job, 0, r);
        if (rep.format == MEL_CLIP_FMT_TEXT)
            text = rep.bytes;
        else
            mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
    }
    if (mel_clip_job_item_count(job) > 1)
        mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);

    const Mel_Alloc* al = mel_clip__alloc();
    if (own->data && al)
        mel_dealloc(al, own->data);
    *own = STR8_EMPTY;
    if (text.len > 0 && al)
    {
        u8* d = (u8*)mel_alloc(al, (usize)text.len);
        if (d)
        {
            memcpy(d, text.data, (usize)text.len);
            *own = (str8){ d, text.len };
        }
    }
    *valid = true;
    if (mel_clip_channel_resolve(mel_clip_job_channel(job)) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY)
        w->prim_seq++;
    else
        w->clip_seq++;
    w->api.display_flush(w->display);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__wl_clear(Mel_Clip_Job* job)
{
    Wl_State*        w = &g_wl;
    bool*            valid = NULL;
    str8*            own = wl_owned(w, mel_clip_job_channel(job), &valid);
    const Mel_Alloc* al = mel_clip__alloc();
    if (own->data && al)
        mel_dealloc(al, own->data);
    *own = STR8_EMPTY;
    *valid = false;
    if (mel_clip_channel_resolve(mel_clip_job_channel(job)) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY)
        w->prim_seq++;
    else
        w->clip_seq++;
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__wl_query(Mel_Clip_Job* job)
{
    Wl_State* w = &g_wl;
    bool*     valid = NULL;
    str8*     own = wl_owned(w, mel_clip_job_channel(job), &valid);
    if (*valid && own->len > 0)
        mel_clip_job_emit_format(job, MEL_CLIP_FMT_TEXT);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__wl_has(Mel_Clip_Job* job)
{
    Wl_State* w = &g_wl;
    bool*     valid = NULL;
    str8*     own = wl_owned(w, mel_clip_job_channel(job), &valid);
    mel_clip_job_set_present(job, *valid && own->len > 0);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void* mel_clip__wl_native(void) { return g_wl.display; }
