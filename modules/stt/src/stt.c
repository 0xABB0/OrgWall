#include "stt_internal.h"

#include <audioin/permission.h>

#include <allocator/allocator.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <collection/list.h>
#include <future/future.h>
#include <executor/executor.h>
#include <string/str8.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

typedef struct
{
    Mel_Stt_Provider_Desc desc;
    u32                   generation;
    bool                  active;
} Provider_Entry;

typedef struct
{
    u32                           provider_idx;
    u64                           stable_id;
    Mel_Stt_Recognizer_Descriptor desc;
    bool                          busy;
} Recognizer_Slot;

typedef struct
{
    u64                stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Stt_Recognizer  recognizer;
    u32                 provider_idx;
    u64                 stable_id;
    u64                 token;
    bool                fed;
    bool                stopping;
    Mel_Stt_On_Result   on_result;
    Mel_Stt_On_Complete on_complete;
    void*               user;
    _Atomic(bool)       resolved;
} Session_Slot;

typedef struct
{
    Mel_Future          future;
    Mel_Task            mic_task;
    Mel_Future*         mic;
    const Mel_Alloc*    alloc;
    const mel_stt_auth* auth;
    _Atomic(u32)        pending;
    bool                resolved;
} Auth_Job;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;

    Mel_SlotMap recognizers;
    Mel_SlotMap sessions;

    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    Mel_Array(Mel_Stt_Session) active_sessions;

    u64 next_token;
    u32 provider_gen;

    Auth_Job* pending_auth;
} Stt;

static Stt g;

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Recognizer_Slot* recognizer_slot(Mel_SlotMap_Handle h) { return (Recognizer_Slot*)mel_slotmap_get(&g.recognizers, h); }

static Session_Slot* session_slot(Mel_SlotMap_Handle h) { return (Session_Slot*)mel_slotmap_get(&g.sessions, h); }

static Reg_Entry* reg_find(u32 prov, u64 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && g.registry.items[i].stable_id == stable_id)
            return &g.registry.items[i];
    return NULL;
}

static void active_session_remove(Mel_Stt_Session s)
{
    for (usize i = 0; i < g.active_sessions.count; i++)
    {
        if (g.active_sessions.items[i].h.index == s.h.index && g.active_sessions.items[i].h.generation == s.h.generation)
        {
            g.active_sessions.items[i] = g.active_sessions.items[g.active_sessions.count - 1];
            g.active_sessions.count--;
            return;
        }
    }
}

static void session_resolve(Mel_SlotMap_Handle h, Session_Slot* ss, Mel_Stt_Status status)
{
    if (atomic_exchange_explicit(&ss->resolved, true, memory_order_acq_rel))
        return;
    Mel_Stt_Session     s = { h };
    Mel_Stt_On_Complete cb = ss->on_complete;
    void*               user = ss->user;
    Recognizer_Slot*    rs = recognizer_slot(ss->recognizer.h);
    if (rs)
        rs->busy = false;
    active_session_remove(s);
    mel_slotmap_remove(&g.sessions, h);
    if (cb)
        cb(s, status, user);
}

static void core_on_result(void* token, const Mel_Stt_Result* result)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Session_Slot*      ss = session_slot(h);
    if (ss && !atomic_load_explicit(&ss->resolved, memory_order_acquire) && ss->on_result)
        ss->on_result((Mel_Stt_Session){ h }, result, ss->user);
}

static void core_on_done(void* token, Mel_Stt_Status status)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Session_Slot*      ss = session_slot(h);
    if (ss)
        session_resolve(h, ss, status);
}

static Mel_Stt_Sink session_sink(Mel_SlotMap_Handle h)
{
    return (Mel_Stt_Sink){
        .on_result = core_on_result,
        .on_done = core_on_done,
        .token = (void*)(usize)mel_slotmap_handle_pack64(h),
    };
}

Mel_Stt_Provider mel_stt_provider_register(const Mel_Stt_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Stt_Provider){ .index = idx, .generation = e.generation };
}

void mel_stt_provider_unregister(Mel_Stt_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_stt_init(const Mel_Alloc* alloc)
{
    assert(!g.initialized);
    assert(alloc != NULL);
    if (g.initialized)
        return;
    g.alloc = alloc;
    mel_slotmap_init(&g.recognizers, g.alloc, .item_size = sizeof(Recognizer_Slot), .initial_capacity = 8);
    mel_slotmap_init(&g.sessions, g.alloc, .item_size = sizeof(Session_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    mel_array_init(&g.active_sessions, g.alloc);
    g.next_token = 0;
    g.provider_gen = 0;
    g.pending_auth = NULL;
    g.initialized = true;
    mel_stt__register_host_providers();
    mel_stt_refresh();
}

void mel_stt_shutdown(void)
{
    assert(g.initialized);
    if (!g.initialized)
        return;
    if (g.pending_auth && !g.pending_auth->resolved)
    {
        g.pending_auth->resolved = true;
        mel_future_cancel(&g.pending_auth->future);
    }
    g.pending_auth = NULL;

    while (g.active_sessions.count > 0)
        mel_stt_abort(g.active_sessions.items[g.active_sessions.count - 1]);

    for (usize i = 0; i < g.providers.count; i++)
        if (g.providers.items[i].active && g.providers.items[i].desc.shutdown)
            g.providers.items[i].desc.shutdown(g.providers.items[i].desc.user, g.alloc);

    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_array_free(&g.active_sessions);
    mel_slotmap_free(&g.recognizers);
    mel_slotmap_free(&g.sessions);
    memset(&g, 0, sizeof g);
}

static void recognizer_lost(Mel_SlotMap_Handle h)
{
    Mel_Stt_Recognizer dev = { h };
    for (usize j = 0; j < g.active_sessions.count;)
    {
        Mel_Stt_Session s = g.active_sessions.items[j];
        Session_Slot*   ss = session_slot(s.h);
        if (ss && mel_stt_recognizer_equal(ss->recognizer, dev))
        {
            session_resolve(s.h, ss, MEL_STT_ERROR | MEL_STT_RESULT_LOST);
            j = 0;
        }
        else
            j++;
    }
}

u32 mel_stt_refresh(void)
{
    if (!g.initialized)
        return 0;

    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.registry.count; i++)
        mel_array_push(&seen, false);

    Mel_Array(Mel_Stt_Recognizer_Raw) tmp;
    mel_array_init(&tmp, g.alloc);
    mel_array_reserve(&tmp, 8);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate_recognizers)
            continue;
        mel_array_clear(&tmp);
        u32 n = pe->desc.enumerate_recognizers(pe->desc.user, g.alloc, tmp.items, (u32)tmp.capacity);
        while (n > tmp.capacity)
        {
            mel_array_reserve(&tmp, n);
            n = pe->desc.enumerate_recognizers(pe->desc.user, g.alloc, tmp.items, (u32)tmp.capacity);
        }
        for (u32 i = 0; i < n; i++)
        {
            Mel_Stt_Recognizer_Raw* raw = &tmp.items[i];
            Reg_Entry*              e = reg_find(pi, raw->stable_id);
            if (e)
            {
                seen.items[(usize)(e - g.registry.items)] = true;
                Recognizer_Slot* s = recognizer_slot(e->handle);
                if (s)
                    s->desc = (Mel_Stt_Recognizer_Descriptor){ .language = raw->language, .caps = raw->caps };
                continue;
            }
            Recognizer_Slot    slot = { .provider_idx = pi, .stable_id = raw->stable_id, .desc = { .language = raw->language, .caps = raw->caps } };
            Mel_SlotMap_Handle h = mel_slotmap_insert(&g.recognizers, &slot);
            Reg_Entry          re = { .stable_id = raw->stable_id, .provider_idx = pi, .handle = h };
            mel_array_push(&g.registry, re);
            mel_array_push(&seen, true);
            mel_log_info("stt", "recognizer added: %.*s stable_id=%llu", (int)raw->language.len, raw->language.data, (unsigned long long)raw->stable_id);
        }
    }

    mel_array_free(&tmp);

    for (usize i = 0; i < g.registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.registry.items[i].handle;
        recognizer_lost(h);
        mel_slotmap_remove(&g.recognizers, h);
        mel_log_info("stt", "recognizer removed: stable_id=%llu", (unsigned long long)g.registry.items[i].stable_id);
        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    mel_array_free(&seen);
    return (u32)g.registry.count;
}

u32 mel_stt_recognizer_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_stt_recognizer_list(Mel_Stt_Recognizer* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Stt_Recognizer){ g.registry.items[i].handle };
    return n;
}

Mel_Stt_Recognizer_Describe_Result mel_stt_recognizer_describe(Mel_Stt_Recognizer r)
{
    Mel_Stt_Recognizer_Describe_Result res = { 0 };
    Recognizer_Slot*                   s = g.initialized ? recognizer_slot(r.h) : NULL;
    if (!s)
    {
        mel_log_error("stt", "describe on dead recognizer handle {index=%u, gen=%u}", r.h.index, r.h.generation);
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
        return res;
    }
    res.value = s->desc;
    res.status = MEL_STT_OK;
    return res;
}

bool mel_stt_recognizer_alive(Mel_Stt_Recognizer r) { return g.initialized && mel_slotmap_alive(&g.recognizers, r.h); }

bool mel_stt_recognizer_equal(Mel_Stt_Recognizer a, Mel_Stt_Recognizer b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

static const mel_stt_auth* mic_auth_as_stt(const mel_audioin_auth* a)
{
    if (a == &mel_audioin_auth_granted)
        return &mel_stt_auth_granted;
    if (a == &mel_audioin_auth_restricted)
        return &mel_stt_auth_restricted;
    if (a == &mel_audioin_auth_denied)
        return &mel_stt_auth_denied;
    return &mel_stt_auth_not_determined;
}

static const mel_stt_auth* most_restrictive(const mel_stt_auth* a, const mel_stt_auth* b) { return a->restrictiveness >= b->restrictiveness ? a : b; }

static bool provider_has_recognizers(u32 idx)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == idx)
            return true;
    return false;
}

static const mel_stt_auth* provider_authorization(const Provider_Entry* pe) { return pe->desc.authorization ? pe->desc.authorization(pe->desc.user) : &mel_stt_auth_granted; }

static const mel_stt_auth* compute_recognition_auth(void)
{
    const mel_stt_auth* worst = NULL;
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !provider_has_recognizers(pi))
            continue;
        const mel_stt_auth* a = provider_authorization(pe);
        if (!worst || a->restrictiveness > worst->restrictiveness)
            worst = a;
    }
    if (worst)
        return worst;
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.authorization)
            continue;
        const mel_stt_auth* a = provider_authorization(pe);
        if (!worst || a->restrictiveness > worst->restrictiveness)
            worst = a;
    }
    return worst ? worst : &mel_stt_auth_not_determined;
}

static const mel_stt_auth* compute_composed_auth(void) { return most_restrictive(compute_recognition_auth(), mic_auth_as_stt(mel_audioin_authorization())); }

const mel_stt_auth* mel_stt_authorization(void)
{
    if (!g.initialized)
        return &mel_stt_auth_not_determined;
    return compute_composed_auth();
}

static void auth_resolve(Auth_Job* j, const mel_stt_auth* auth)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->auth = auth;
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_future_resolve(&j->future, (void*)auth, mel_stt_auth_is_granted(auth) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR);
}

static void auth_step(Auth_Job* j)
{
    if (!j)
        return;
    if (atomic_fetch_sub_explicit(&j->pending, 1u, memory_order_acq_rel) == 1u)
        auth_resolve(j, compute_composed_auth());
}

static void core_on_auth(void* token, const mel_stt_auth* auth)
{
    MEL_UNUSED(auth);
    auth_step(token);
}

static void mic_task_run(Mel_Task* self)
{
    Auth_Job* j = mel_container_of(self, Auth_Job, mic_task);
    auth_step(j);
}

Mel_Future* mel_stt_authorize(const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    const Mel_Alloc* alloc = a ? a : g.alloc;
    Auth_Job*        j = mel_alloc_type(alloc, Auth_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->alloc = alloc;
    mel_future_init(&j->future, NULL, alloc);
    mel_task_init(&j->mic_task, mic_task_run);
    g.pending_auth = j;

    u32 prompters = 0;
    for (u32 pi = 0; pi < g.providers.count; pi++)
        if (g.providers.items[pi].active && g.providers.items[pi].desc.authorize)
            prompters++;

    Mel_Future* mic = mel_audioin_authorize(alloc);
    u32         waits = prompters + (mic ? 1u : 0u);
    if (waits == 0)
    {
        auth_resolve(j, compute_composed_auth());
        return &j->future;
    }

    j->mic = mic;
    atomic_store_explicit(&j->pending, waits, memory_order_release);

    Mel_Stt_Sink sink = { .on_auth = core_on_auth, .token = j };
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (pe->active && pe->desc.authorize)
            pe->desc.authorize(pe->desc.user, sink);
    }
    if (mic)
        mel_future_then(mic, &j->mic_task, mel_executor_inline());
    return &j->future;
}

const mel_stt_auth* mel_stt_future_auth(const Mel_Future* f)
{
    const mel_stt_auth* a = f ? (const mel_stt_auth*)mel_future_value((Mel_Future*)f) : NULL;
    return a ? a : &mel_stt_auth_not_determined;
}

void mel_stt_future_free(Mel_Future* f)
{
    if (!f)
        return;
    Auth_Job* j = mel_container_of(f, Auth_Job, future);
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    if (j->mic)
        mel_audioin_future_free(j->mic);
    mel_dealloc(j->alloc, j);
}

Mel_Stt_Listen_Result mel_stt_listen_opt(Mel_Stt_Recognizer r, Mel_Stt_Listen_Opt opt)
{
    Mel_Stt_Listen_Result res = { .value = MEL_STT_SESSION_NULL, .status = MEL_STT_ERROR };
    if (!g.initialized)
    {
        mel_log_error("stt", "listen before init");
        return res;
    }
    Recognizer_Slot* rs = recognizer_slot(r.h);
    if (!rs)
    {
        mel_log_error("stt", "listen on dead recognizer handle {index=%u, gen=%u}", r.h.index, r.h.generation);
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
        return res;
    }
    Provider_Entry* prov = provider_get(rs->provider_idx);
    if (!prov || !prov->desc.listen)
    {
        mel_log_error("stt", "recognizer has no listen provider");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (!opt.on_result)
    {
        mel_log_error("stt", "listen without on_result; a session with no consumer is a bug");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (rs->busy)
    {
        mel_log_error("stt", "recognizer already has a live session");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_BUSY;
        return res;
    }

    Mel_Stt_Recognizer_Caps caps = rs->desc.caps;
    bool                    device_door = mel_slotmap_handle_valid(opt.device.h);

    if (opt.feed && device_door)
    {
        mel_log_error("stt", "listen with both feed and device; the three doors select exactly one source");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (opt.feed && opt.feed_sample_rate == 0)
    {
        mel_log_error("stt", "listen with feed but feed_sample_rate zero");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (!opt.feed && opt.feed_sample_rate != 0)
    {
        mel_log_error("stt", "listen with feed_sample_rate but feed false");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (opt.feed && !caps.feed)
    {
        mel_log_error("stt", "recognizer does not accept fed PCM");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (device_door && !caps.device_select)
    {
        mel_log_error("stt", "recognizer cannot select a capture device; never a silent fall-back to default");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (device_door && !mel_audioin_alive(opt.device))
    {
        mel_log_error("stt", "listen on dead audioin device {index=%u, gen=%u}", opt.device.h.index, opt.device.h.generation);
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
        return res;
    }
    if (opt.require_on_device && !caps.require_on_device)
    {
        mel_log_error("stt", "recognizer cannot guarantee on-device recognition; privacy promises are never best-effort");
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (opt.vocabulary_count > 0 && opt.vocabulary == NULL)
    {
        mel_log_error("stt", "listen with vocabulary_count %u but NULL vocabulary", opt.vocabulary_count);
        res.status = MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        return res;
    }
    if (!opt.feed)
    {
        const mel_stt_auth* auth = compute_composed_auth();
        if (!mel_stt_auth_is_granted(auth))
        {
            mel_log_error("stt", "microphone doors need consent; composed authorization is %s", mel_stt_auth_name(auth));
            res.status = MEL_STT_ERROR | MEL_STT_RESULT_DENIED;
            return res;
        }
    }

    Mel_Stt_Status warn = 0;
    bool           partials = opt.partials;
    if (partials && !caps.partials)
    {
        partials = false;
        warn |= MEL_STT_WARN_PARTIALS_DROPPED;
        mel_log_warn("stt", "partials requested but recognizer reports none; dropped");
    }
    const str8* vocabulary = opt.vocabulary;
    u32         vocabulary_count = opt.vocabulary_count;
    if (vocabulary_count > 0 && !caps.vocabulary)
    {
        vocabulary = NULL;
        vocabulary_count = 0;
        warn |= MEL_STT_WARN_VOCABULARY_DROPPED;
        mel_log_warn("stt", "vocabulary biasing unsupported by recognizer; dropped");
    }
    bool punctuation = opt.punctuation;
    if (punctuation && !caps.punctuation)
    {
        punctuation = false;
        warn |= MEL_STT_WARN_PUNCTUATION_DROPPED;
        mel_log_warn("stt", "automatic punctuation unsupported by recognizer; dropped");
    }
    bool profanity = opt.profanity_filter;
    if (profanity && !caps.profanity_filter)
    {
        profanity = false;
        warn |= MEL_STT_WARN_PROFANITY_DROPPED;
        mel_log_warn("stt", "profanity filter unsupported by recognizer; dropped");
    }

    Mel_AudioIn_Describe_Result dev = { 0 };
    str8                        device_stable_id = STR8_EMPTY;
    if (device_door)
    {
        dev = mel_audioin_describe(opt.device, g.alloc);
        if (mel_audioin_status_failed(dev.status))
        {
            res.status = MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
            return res;
        }
        device_stable_id = dev.value.stable_id;
    }

    Session_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.recognizer = r;
    slot.provider_idx = rs->provider_idx;
    slot.stable_id = rs->stable_id;
    slot.token = ++g.next_token;
    slot.fed = opt.feed;
    slot.on_result = opt.on_result;
    slot.on_complete = opt.on_complete;
    slot.user = opt.user;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.sessions, &slot);
    rs->busy = true;

    Mel_Stt_Listen_Lowered lowered = {
        .partials = partials,
        .device_stable_id = device_stable_id,
        .feed = opt.feed,
        .feed_sample_rate = opt.feed_sample_rate,
        .require_on_device = opt.require_on_device,
        .vocabulary = vocabulary,
        .vocabulary_count = vocabulary_count,
        .punctuation = punctuation,
        .profanity_filter = profanity,
        .caps = caps,
    };
    Mel_Stt_Status sub = prov->desc.listen(prov->desc.user, rs->stable_id, slot.token, &lowered, session_sink(h));
    if (device_door)
        mel_audioin_describe_free(&dev);
    if (mel_stt_failed(sub))
    {
        rs->busy = false;
        mel_slotmap_remove(&g.sessions, h);
        res.status = sub;
        return res;
    }

    Mel_Stt_Session s = { h };
    if (mel_slotmap_alive(&g.sessions, h))
        mel_array_push(&g.active_sessions, s);
    res.value = s;
    warn |= sub & ~MEL_STT_SEVERITY_MASK;
    res.status = warn || mel_stt_warned(sub) ? (warn | MEL_STT_WARNED) : MEL_STT_OK;
    return res;
}

Mel_Stt_Status mel_stt_feed(Mel_Stt_Session s, const f32* frames, u32 frame_count)
{
    if (!g.initialized)
    {
        mel_log_error("stt", "feed before init");
        return MEL_STT_ERROR;
    }
    Session_Slot* ss = session_slot(s.h);
    if (!ss || atomic_load_explicit(&ss->resolved, memory_order_acquire))
    {
        mel_log_error("stt", "feed on dead or finished session");
        return MEL_STT_ERROR | MEL_STT_RESULT_LOST;
    }
    if (!ss->fed)
    {
        mel_log_error("stt", "feed on a session not opened through the fed door");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    assert(frames != NULL);
    if (frames == NULL)
    {
        mel_log_error("stt", "feed with NULL frames");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (!prov || !prov->desc.feed)
    {
        mel_log_error("stt", "provider claims feed capability but exposes no feed entry; provider bug");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    return prov->desc.feed(prov->desc.user, ss->stable_id, ss->token, frames, frame_count);
}

Mel_Stt_Status mel_stt_stop(Mel_Stt_Session s)
{
    if (!g.initialized)
    {
        mel_log_error("stt", "stop before init");
        return MEL_STT_ERROR;
    }
    Session_Slot* ss = session_slot(s.h);
    if (!ss || atomic_load_explicit(&ss->resolved, memory_order_acquire))
    {
        mel_log_error("stt", "stop on dead or finished session");
        return MEL_STT_ERROR | MEL_STT_RESULT_LOST;
    }
    if (ss->stopping)
        return MEL_STT_OK;
    Recognizer_Slot* rs = recognizer_slot(ss->recognizer.h);
    Provider_Entry*  prov = provider_get(ss->provider_idx);
    bool             can_stop = rs && rs->desc.caps.can_stop;
    if (can_stop && (!prov || !prov->desc.stop))
    {
        mel_log_error("stt", "provider claims can_stop but exposes no stop entry; synthesizing stop");
        can_stop = false;
    }
    if (can_stop)
    {
        ss->stopping = true;
        prov->desc.stop(prov->desc.user, ss->stable_id, ss->token);
        return MEL_STT_OK;
    }
    mel_log_warn("stt", "recognizer cannot drain; stop synthesized as abort");
    if (prov && prov->desc.abort)
        prov->desc.abort(prov->desc.user, ss->stable_id, ss->token);
    session_resolve(s.h, ss, MEL_STT_OK | MEL_STT_RESULT_ABORTED);
    return MEL_STT_WARNED | MEL_STT_WARN_STOP_SYNTHESIZED;
}

void mel_stt_abort(Mel_Stt_Session s)
{
    if (!g.initialized)
        return;
    Session_Slot* ss = session_slot(s.h);
    if (!ss)
        return;
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (prov && prov->desc.abort)
        prov->desc.abort(prov->desc.user, ss->stable_id, ss->token);
    session_resolve(s.h, ss, MEL_STT_OK | MEL_STT_RESULT_ABORTED);
}

bool mel_stt_listening(Mel_Stt_Session s)
{
    Session_Slot* ss = g.initialized ? session_slot(s.h) : NULL;
    return ss && !atomic_load_explicit(&ss->resolved, memory_order_acquire);
}

void* mel_stt_recognizer_native(Mel_Stt_Recognizer r)
{
    if (!g.initialized)
        return NULL;
    Recognizer_Slot* rs = recognizer_slot(r.h);
    if (!rs)
        return NULL;
    Provider_Entry* prov = provider_get(rs->provider_idx);
    return (prov && prov->desc.recognizer_native) ? prov->desc.recognizer_native(prov->desc.user, rs->stable_id) : NULL;
}
