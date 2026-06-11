#include <speech/provider.h>

#include "descriptors_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <collection/list.h>
#include <future/future.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Speech_Provider_Desc desc;
    u32                      generation;
    bool                     active;
} Provider_Entry;

typedef struct
{
    u32                         provider_idx;
    u64                         stable_id;
    Mel_Speech_Voice_Descriptor desc;
} Voice_Slot;

typedef struct
{
    u32                              provider_idx;
    u64                              stable_id;
    Mel_Speech_Recognizer_Descriptor desc;
    bool                             busy;
} Recognizer_Slot;

typedef struct
{
    u64                stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef Mel_Array(Reg_Entry) Reg_Array;

typedef struct
{
    Mel_Speech_Voice             voice;
    u32                          provider_idx;
    u64                          stable_id;
    u64                          token;
    Mel_Speech_Status            warnings;
    Mel_Speech_On_Speak_Complete on_complete;
    Mel_Speech_On_Range          on_range;
    void*                        user;
    bool                         paused;
    bool                         resolved;
} Utterance_Slot;

typedef struct
{
    Mel_Speech_Recognizer         recognizer;
    u32                           provider_idx;
    u64                           stable_id;
    u64                           token;
    Mel_Speech_Status             warnings;
    Mel_Speech_On_Result          on_result;
    Mel_Speech_On_Listen_Complete on_complete;
    void*                         user;
    bool                          stopping;
    bool                          resolved;
} Session_Slot;

typedef struct
{
    Mel_Future             future;
    const Mel_Alloc*       alloc;
    const mel_speech_auth* auth;
    bool                   resolved;
} Auth_Job;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;

    Mel_SlotMap voices;
    Mel_SlotMap recognizers;
    Mel_SlotMap utterances;
    Mel_SlotMap sessions;

    Mel_Array(Provider_Entry) providers;
    Reg_Array voice_registry;
    Reg_Array recognizer_registry;
    Mel_Array(Mel_Speech_Utterance) active_utterances;
    Mel_Array(Mel_Speech_Session) active_sessions;

    u64 next_token;
    u32 provider_gen;

    Auth_Job* pending_auth;
} Speech;

static Speech g;

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Voice_Slot* voice_slot(Mel_SlotMap_Handle h) { return (Voice_Slot*)mel_slotmap_get(&g.voices, h); }

static Recognizer_Slot* recognizer_slot(Mel_SlotMap_Handle h) { return (Recognizer_Slot*)mel_slotmap_get(&g.recognizers, h); }

static Reg_Entry* reg_find(Reg_Array* reg, u32 prov, u64 stable_id)
{
    for (usize i = 0; i < reg->count; i++)
        if (reg->items[i].provider_idx == prov && reg->items[i].stable_id == stable_id)
            return &reg->items[i];
    return NULL;
}

static void active_utterance_remove(Mel_Speech_Utterance u)
{
    for (usize i = 0; i < g.active_utterances.count; i++)
    {
        if (g.active_utterances.items[i].h.index == u.h.index && g.active_utterances.items[i].h.generation == u.h.generation)
        {
            g.active_utterances.items[i] = g.active_utterances.items[g.active_utterances.count - 1];
            g.active_utterances.count--;
            return;
        }
    }
}

static void active_session_remove(Mel_Speech_Session s)
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

static void utterance_resolve(Mel_SlotMap_Handle h, Utterance_Slot* us, Mel_Speech_Status status)
{
    if (us->resolved)
        return;
    us->resolved = true;
    Mel_Speech_Utterance         u = { h };
    Mel_Speech_On_Speak_Complete cb = us->on_complete;
    void*                        user = us->user;
    active_utterance_remove(u);
    mel_slotmap_remove(&g.utterances, h);
    if (cb)
        cb(u, status, user);
}

static void session_resolve(Mel_SlotMap_Handle h, Session_Slot* ss, Mel_Speech_Status status)
{
    if (ss->resolved)
        return;
    ss->resolved = true;
    Mel_Speech_Session            s = { h };
    Mel_Speech_On_Listen_Complete cb = ss->on_complete;
    void*                         user = ss->user;
    Recognizer_Slot*              rs = recognizer_slot(ss->recognizer.h);
    if (rs)
        rs->busy = false;
    active_session_remove(s);
    mel_slotmap_remove(&g.sessions, h);
    if (cb)
        cb(s, status, user);
}

static void core_on_range(void* token, Mel_Speech_Range range)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Utterance_Slot*    us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, h);
    if (us && !us->resolved && us->on_range)
        us->on_range((Mel_Speech_Utterance){ h }, range, us->user);
}

static void core_on_speak_done(void* token, Mel_Speech_Status status)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Utterance_Slot*    us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, h);
    if (us && !us->resolved)
        utterance_resolve(h, us, status);
}

static void core_on_result(void* token, const Mel_Speech_Result* result)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Session_Slot*      ss = (Session_Slot*)mel_slotmap_get(&g.sessions, h);
    if (ss && !ss->resolved && ss->on_result)
        ss->on_result((Mel_Speech_Session){ h }, result, ss->user);
}

static void core_on_listen_done(void* token, Mel_Speech_Status status)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Session_Slot*      ss = (Session_Slot*)mel_slotmap_get(&g.sessions, h);
    if (ss && !ss->resolved)
        session_resolve(h, ss, status);
}

static void auth_resolve(Auth_Job* j, const mel_speech_auth* auth)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->auth = auth;
    Mel_Future_Status fs = mel_speech_auth_is_granted(auth) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR;
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_future_resolve(&j->future, (void*)auth, fs);
}

static void core_on_auth(void* token, const mel_speech_auth* auth)
{
    MEL_UNUSED(token);
    auth_resolve(g.pending_auth, auth);
}

static Mel_Speech_Sink utterance_sink(Mel_SlotMap_Handle h)
{
    return (Mel_Speech_Sink){
        .on_range = core_on_range,
        .on_speak_done = core_on_speak_done,
        .token = (void*)(usize)mel_slotmap_handle_pack64(h),
    };
}

static Mel_Speech_Sink session_sink(Mel_SlotMap_Handle h)
{
    return (Mel_Speech_Sink){
        .on_result = core_on_result,
        .on_listen_done = core_on_listen_done,
        .token = (void*)(usize)mel_slotmap_handle_pack64(h),
    };
}

Mel_Speech_Provider mel_speech_provider_register(const Mel_Speech_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Speech_Provider){ .index = idx, .generation = e.generation };
}

void mel_speech_provider_unregister(Mel_Speech_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_speech_init(const Mel_Alloc* alloc)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    mel_slotmap_init(&g.voices, g.alloc, .item_size = sizeof(Voice_Slot), .initial_capacity = 8);
    mel_slotmap_init(&g.recognizers, g.alloc, .item_size = sizeof(Recognizer_Slot), .initial_capacity = 8);
    mel_slotmap_init(&g.utterances, g.alloc, .item_size = sizeof(Utterance_Slot), .initial_capacity = 8);
    mel_slotmap_init(&g.sessions, g.alloc, .item_size = sizeof(Session_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.voice_registry, g.alloc);
    mel_array_init(&g.recognizer_registry, g.alloc);
    mel_array_init(&g.active_utterances, g.alloc);
    mel_array_init(&g.active_sessions, g.alloc);
    g.next_token = 0;
    g.provider_gen = 0;
    g.pending_auth = NULL;
    g.initialized = true;
    mel_speech__register_host_providers();
    mel_speech_refresh();
}

void mel_speech_shutdown(void)
{
    if (!g.initialized)
        return;
    if (g.pending_auth && !g.pending_auth->resolved)
    {
        g.pending_auth->resolved = true;
        mel_future_cancel(&g.pending_auth->future);
    }
    g.pending_auth = NULL;

    while (g.active_utterances.count > 0)
        mel_speech_speak_abort(g.active_utterances.items[g.active_utterances.count - 1]);
    while (g.active_sessions.count > 0)
        mel_speech_listen_abort(g.active_sessions.items[g.active_sessions.count - 1]);

    for (usize i = 0; i < g.providers.count; i++)
        if (g.providers.items[i].desc.shutdown)
            g.providers.items[i].desc.shutdown(g.providers.items[i].desc.user, g.alloc);

    mel_array_free(&g.voice_registry);
    mel_array_free(&g.recognizer_registry);
    mel_array_free(&g.providers);
    mel_array_free(&g.active_utterances);
    mel_array_free(&g.active_sessions);
    mel_slotmap_free(&g.voices);
    mel_slotmap_free(&g.recognizers);
    mel_slotmap_free(&g.utterances);
    mel_slotmap_free(&g.sessions);
    memset(&g, 0, sizeof g);
}

static void voice_lost(Mel_SlotMap_Handle h)
{
    Mel_Speech_Voice dev = { h };
    for (usize j = 0; j < g.active_utterances.count;)
    {
        Mel_Speech_Utterance u = g.active_utterances.items[j];
        Utterance_Slot*      us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, u.h);
        if (us && mel_speech_voice_equal(us->voice, dev))
        {
            utterance_resolve(u.h, us, MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_LOST);
            j = 0;
        }
        else
            j++;
    }
}

static void recognizer_lost(Mel_SlotMap_Handle h)
{
    Mel_Speech_Recognizer dev = { h };
    for (usize j = 0; j < g.active_sessions.count;)
    {
        Mel_Speech_Session s = g.active_sessions.items[j];
        Session_Slot*      ss = (Session_Slot*)mel_slotmap_get(&g.sessions, s.h);
        if (ss && mel_speech_recognizer_equal(ss->recognizer, dev))
        {
            session_resolve(s.h, ss, MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_LOST);
            j = 0;
        }
        else
            j++;
    }
}

static void refresh_voices(void)
{
    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.voice_registry.count; i++)
        mel_array_push(&seen, false);

    Mel_Array(Mel_Speech_Voice_Raw) tmp;
    mel_array_init(&tmp, g.alloc);
    mel_array_reserve(&tmp, 8);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate_voices)
            continue;
        mel_array_clear(&tmp);
        u32 n = pe->desc.enumerate_voices(pe->desc.user, g.alloc, tmp.items, (u32)tmp.capacity);
        while (n > tmp.capacity)
        {
            mel_array_reserve(&tmp, n);
            n = pe->desc.enumerate_voices(pe->desc.user, g.alloc, tmp.items, (u32)tmp.capacity);
        }
        for (u32 i = 0; i < n; i++)
        {
            Mel_Speech_Voice_Raw* raw = &tmp.items[i];
            Reg_Entry*            e = reg_find(&g.voice_registry, pi, raw->stable_id);
            if (e)
            {
                seen.items[(usize)(e - g.voice_registry.items)] = true;
                Voice_Slot* s = voice_slot(e->handle);
                if (s)
                    s->desc = (Mel_Speech_Voice_Descriptor){ .name = raw->name, .language = raw->language, .caps = raw->caps };
                continue;
            }
            Voice_Slot         slot = { .provider_idx = pi, .stable_id = raw->stable_id, .desc = { .name = raw->name, .language = raw->language, .caps = raw->caps } };
            Mel_SlotMap_Handle h = mel_slotmap_insert(&g.voices, &slot);
            Reg_Entry          re = { .stable_id = raw->stable_id, .provider_idx = pi, .handle = h };
            mel_array_push(&g.voice_registry, re);
            mel_array_push(&seen, true);
        }
    }

    mel_array_free(&tmp);

    for (usize i = 0; i < g.voice_registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.voice_registry.items[i].handle;
        voice_lost(h);
        mel_slotmap_remove(&g.voices, h);
        mel_log_info("speech", "voice removed: stable_id=%llu", (unsigned long long)g.voice_registry.items[i].stable_id);
        usize last = g.voice_registry.count - 1;
        g.voice_registry.items[i] = g.voice_registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.voice_registry.count--;
    }

    mel_array_free(&seen);
}

static void refresh_recognizers(void)
{
    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.recognizer_registry.count; i++)
        mel_array_push(&seen, false);

    Mel_Array(Mel_Speech_Recognizer_Raw) tmp;
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
            Mel_Speech_Recognizer_Raw* raw = &tmp.items[i];
            Reg_Entry*                 e = reg_find(&g.recognizer_registry, pi, raw->stable_id);
            if (e)
            {
                seen.items[(usize)(e - g.recognizer_registry.items)] = true;
                Recognizer_Slot* s = recognizer_slot(e->handle);
                if (s)
                    s->desc = (Mel_Speech_Recognizer_Descriptor){ .language = raw->language, .caps = raw->caps };
                continue;
            }
            Recognizer_Slot    slot = { .provider_idx = pi, .stable_id = raw->stable_id, .desc = { .language = raw->language, .caps = raw->caps } };
            Mel_SlotMap_Handle h = mel_slotmap_insert(&g.recognizers, &slot);
            Reg_Entry          re = { .stable_id = raw->stable_id, .provider_idx = pi, .handle = h };
            mel_array_push(&g.recognizer_registry, re);
            mel_array_push(&seen, true);
        }
    }

    mel_array_free(&tmp);

    for (usize i = 0; i < g.recognizer_registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.recognizer_registry.items[i].handle;
        recognizer_lost(h);
        mel_slotmap_remove(&g.recognizers, h);
        mel_log_info("speech", "recognizer removed: stable_id=%llu", (unsigned long long)g.recognizer_registry.items[i].stable_id);
        usize last = g.recognizer_registry.count - 1;
        g.recognizer_registry.items[i] = g.recognizer_registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.recognizer_registry.count--;
    }

    mel_array_free(&seen);
}

u32 mel_speech_refresh(void)
{
    if (!g.initialized)
        return 0;
    refresh_voices();
    refresh_recognizers();
    return (u32)(g.voice_registry.count + g.recognizer_registry.count);
}

u32 mel_speech_voice_count(void) { return g.initialized ? (u32)g.voice_registry.count : 0; }

u32 mel_speech_voice_list(Mel_Speech_Voice* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.voice_registry.count < cap ? (u32)g.voice_registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Speech_Voice){ g.voice_registry.items[i].handle };
    return n;
}

Mel_Speech_Voice_Describe_Result mel_speech_voice_describe(Mel_Speech_Voice v)
{
    Mel_Speech_Voice_Describe_Result r = { 0 };
    Voice_Slot*                      s = g.initialized ? voice_slot(v.h) : NULL;
    if (!s)
    {
        mel_log_error("speech", "voice describe on dead handle {index=%u, gen=%u}", v.h.index, v.h.generation);
        r.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
        return r;
    }
    r.value = s->desc;
    r.status = MEL_SPEECH_OK;
    return r;
}

bool mel_speech_voice_alive(Mel_Speech_Voice v) { return g.initialized && mel_slotmap_alive(&g.voices, v.h); }

bool mel_speech_voice_equal(Mel_Speech_Voice a, Mel_Speech_Voice b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

u32 mel_speech_recognizer_count(void) { return g.initialized ? (u32)g.recognizer_registry.count : 0; }

u32 mel_speech_recognizer_list(Mel_Speech_Recognizer* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.recognizer_registry.count < cap ? (u32)g.recognizer_registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Speech_Recognizer){ g.recognizer_registry.items[i].handle };
    return n;
}

Mel_Speech_Recognizer_Describe_Result mel_speech_recognizer_describe(Mel_Speech_Recognizer r)
{
    Mel_Speech_Recognizer_Describe_Result res = { 0 };
    Recognizer_Slot*                      s = g.initialized ? recognizer_slot(r.h) : NULL;
    if (!s)
    {
        mel_log_error("speech", "recognizer describe on dead handle {index=%u, gen=%u}", r.h.index, r.h.generation);
        res.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
        return res;
    }
    res.value = s->desc;
    res.status = MEL_SPEECH_OK;
    return res;
}

bool mel_speech_recognizer_alive(Mel_Speech_Recognizer r) { return g.initialized && mel_slotmap_alive(&g.recognizers, r.h); }

bool mel_speech_recognizer_equal(Mel_Speech_Recognizer a, Mel_Speech_Recognizer b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

static const mel_speech_auth* first_provider_authorization(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* pe = &g.providers.items[i];
        if (pe->active && pe->desc.authorization)
            return pe->desc.authorization(pe->desc.user);
    }
    return &mel_speech_auth_not_determined;
}

const mel_speech_auth* mel_speech_authorization(void)
{
    if (!g.initialized)
        return &mel_speech_auth_not_determined;
    return first_provider_authorization();
}

Mel_Future* mel_speech_authorize(const Mel_Alloc* a)
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
    g.pending_auth = j;

    Provider_Entry* prov = NULL;
    for (u32 i = 0; i < g.providers.count; i++)
    {
        if (g.providers.items[i].active && g.providers.items[i].desc.authorize)
        {
            prov = &g.providers.items[i];
            break;
        }
    }
    if (!prov)
    {
        auth_resolve(j, first_provider_authorization());
        return &j->future;
    }
    Mel_Speech_Sink sink = { .on_auth = core_on_auth, .token = NULL };
    prov->desc.authorize(prov->desc.user, sink);
    return &j->future;
}

const mel_speech_auth* mel_speech_future_auth(const Mel_Future* f)
{
    const mel_speech_auth* a = f ? (const mel_speech_auth*)mel_future_value((Mel_Future*)f) : NULL;
    return a ? a : &mel_speech_auth_not_determined;
}

void mel_speech_future_free(Mel_Future* f)
{
    if (!f)
        return;
    Auth_Job* j = mel_container_of(f, Auth_Job, future);
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_dealloc(j->alloc, j);
}

Mel_Speech_Speak_Result mel_speech_speak_opt(Mel_Speech_Voice v, str8 text, Mel_Speech_Speak_Opt opt)
{
    Mel_Speech_Speak_Result r = { .value = MEL_SPEECH_UTTERANCE_NULL, .status = MEL_SPEECH_ERROR };
    if (!g.initialized)
        return r;
    Voice_Slot* vs = voice_slot(v.h);
    if (!vs)
    {
        mel_log_error("speech", "speak on dead voice handle");
        r.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
        return r;
    }
    Provider_Entry* prov = provider_get(vs->provider_idx);
    if (!prov || !prov->desc.speak)
    {
        mel_log_error("speech", "voice has no speak provider");
        r.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
        return r;
    }
    if (str8_is_empty(text))
    {
        mel_log_error("speech", "speak with empty text");
        r.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
        return r;
    }

    Mel_Speech_Voice_Caps caps = vs->desc.caps;
    Mel_Speech_Status     warn = 0;
    f32                   rate = opt.rate;
    f32                   pitch = opt.pitch;
    f32                   volume = opt.volume;

    if (rate != 0.0f)
    {
        if (!caps.rate)
        {
            rate = 0.0f;
            warn |= MEL_SPEECH_WARN_RATE_CLAMPED;
        }
        else if (caps.rate_max > 0.0f && (rate < caps.rate_min || rate > caps.rate_max))
        {
            rate = rate < caps.rate_min ? caps.rate_min : caps.rate_max;
            warn |= MEL_SPEECH_WARN_RATE_CLAMPED;
        }
    }
    if (pitch != 0.0f && !caps.pitch)
    {
        pitch = 0.0f;
        warn |= MEL_SPEECH_WARN_PITCH_DROPPED;
    }
    if (volume != 0.0f && !caps.volume)
    {
        volume = 0.0f;
        warn |= MEL_SPEECH_WARN_VOLUME_DROPPED;
    }
    bool want_ranges = opt.on_range != NULL;
    if (want_ranges && !caps.ranges)
    {
        want_ranges = false;
        warn |= MEL_SPEECH_WARN_RANGES_DROPPED;
    }

    Utterance_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.voice = v;
    slot.provider_idx = vs->provider_idx;
    slot.stable_id = vs->stable_id;
    slot.token = ++g.next_token;
    slot.warnings = warn;
    slot.on_complete = opt.on_complete;
    slot.on_range = want_ranges ? opt.on_range : NULL;
    slot.user = opt.user;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.utterances, &slot);

    Mel_Speech_Speak_Lowered lowered = { .text = text, .rate = rate, .pitch = pitch, .volume = volume, .want_ranges = want_ranges, .caps = caps };
    Mel_Speech_Status        sub = prov->desc.speak(prov->desc.user, vs->stable_id, slot.token, &lowered, utterance_sink(h));
    if (mel_speech_failed(sub))
    {
        mel_slotmap_remove(&g.utterances, h);
        r.status = sub;
        return r;
    }

    Mel_Speech_Utterance u = { h };
    if (mel_slotmap_alive(&g.utterances, h))
        mel_array_push(&g.active_utterances, u);
    r.value = u;
    r.status = warn ? (warn | MEL_SPEECH_WARNED) : MEL_SPEECH_OK;
    return r;
}

Mel_Speech_Status mel_speech_speak_pause(Mel_Speech_Utterance u)
{
    if (!g.initialized)
        return MEL_SPEECH_ERROR;
    Utterance_Slot* us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, u.h);
    if (!us || us->resolved)
    {
        mel_log_error("speech", "pause on dead or finished utterance");
        return MEL_SPEECH_ERROR;
    }
    if (us->paused)
        return MEL_SPEECH_OK;
    Voice_Slot* vs = voice_slot(us->voice.h);
    if (!vs || !vs->desc.caps.can_pause)
    {
        mel_log_error("speech", "voice cannot pause");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    Provider_Entry* prov = provider_get(us->provider_idx);
    if (!prov || !prov->desc.speak_pause)
    {
        mel_log_error("speech", "voice has no pause provider");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    prov->desc.speak_pause(prov->desc.user, us->stable_id, us->token);
    us->paused = true;
    return MEL_SPEECH_OK;
}

Mel_Speech_Status mel_speech_speak_resume(Mel_Speech_Utterance u)
{
    if (!g.initialized)
        return MEL_SPEECH_ERROR;
    Utterance_Slot* us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, u.h);
    if (!us || us->resolved)
    {
        mel_log_error("speech", "resume on dead or finished utterance");
        return MEL_SPEECH_ERROR;
    }
    if (!us->paused)
        return MEL_SPEECH_OK;
    Provider_Entry* prov = provider_get(us->provider_idx);
    if (!prov || !prov->desc.speak_resume)
    {
        mel_log_error("speech", "voice has no resume provider");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    prov->desc.speak_resume(prov->desc.user, us->stable_id, us->token);
    us->paused = false;
    return MEL_SPEECH_OK;
}

void mel_speech_speak_abort(Mel_Speech_Utterance u)
{
    if (!g.initialized)
        return;
    Utterance_Slot* us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, u.h);
    if (!us || us->resolved)
        return;
    Provider_Entry* prov = provider_get(us->provider_idx);
    if (prov && prov->desc.speak_abort)
        prov->desc.speak_abort(prov->desc.user, us->stable_id, us->token);
    utterance_resolve(u.h, us, MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED);
}

void mel_speech_speak_abort_all(Mel_Speech_Voice v)
{
    if (!g.initialized)
        return;
    Mel_Array(Mel_Speech_Utterance) snap;
    mel_array_init(&snap, g.alloc);
    for (usize i = 0; i < g.active_utterances.count; i++)
    {
        Utterance_Slot* us = (Utterance_Slot*)mel_slotmap_get(&g.utterances, g.active_utterances.items[i].h);
        if (us && mel_speech_voice_equal(us->voice, v))
            mel_array_push(&snap, g.active_utterances.items[i]);
    }
    for (usize i = 0; i < snap.count; i++)
        mel_speech_speak_abort(snap.items[i]);
    mel_array_free(&snap);
}

bool mel_speech_speaking(Mel_Speech_Utterance u)
{
    Utterance_Slot* us = g.initialized ? (Utterance_Slot*)mel_slotmap_get(&g.utterances, u.h) : NULL;
    return us && !us->resolved && !us->paused;
}

bool mel_speech_speak_paused(Mel_Speech_Utterance u)
{
    Utterance_Slot* us = g.initialized ? (Utterance_Slot*)mel_slotmap_get(&g.utterances, u.h) : NULL;
    return us && !us->resolved && us->paused;
}

Mel_Speech_Listen_Result mel_speech_listen_opt(Mel_Speech_Recognizer r, Mel_Speech_Listen_Opt opt)
{
    Mel_Speech_Listen_Result res = { .value = MEL_SPEECH_SESSION_NULL, .status = MEL_SPEECH_ERROR };
    if (!g.initialized)
        return res;
    Recognizer_Slot* rs = recognizer_slot(r.h);
    if (!rs)
    {
        mel_log_error("speech", "listen on dead recognizer handle");
        res.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
        return res;
    }
    Provider_Entry* prov = provider_get(rs->provider_idx);
    if (!prov || !prov->desc.listen)
    {
        mel_log_error("speech", "recognizer has no listen provider");
        res.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
        return res;
    }
    if (!opt.on_result)
    {
        mel_log_error("speech", "listen without on_result callback");
        res.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
        return res;
    }
    if (rs->busy)
    {
        mel_log_error("speech", "recognizer already has a live session");
        res.status = MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_BUSY;
        return res;
    }

    Mel_Speech_Recognizer_Caps caps = rs->desc.caps;
    Mel_Speech_Status          warn = 0;
    bool                       partials = opt.partials;
    if (partials && !caps.partials)
    {
        partials = false;
        warn |= MEL_SPEECH_WARN_PARTIALS_DROPPED;
    }

    Session_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.recognizer = r;
    slot.provider_idx = rs->provider_idx;
    slot.stable_id = rs->stable_id;
    slot.token = ++g.next_token;
    slot.warnings = warn;
    slot.on_result = opt.on_result;
    slot.on_complete = opt.on_complete;
    slot.user = opt.user;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.sessions, &slot);
    rs->busy = true;

    Mel_Speech_Listen_Lowered lowered = { .partials = partials, .caps = caps };
    Mel_Speech_Status         sub = prov->desc.listen(prov->desc.user, rs->stable_id, slot.token, &lowered, session_sink(h));
    if (mel_speech_failed(sub))
    {
        rs->busy = false;
        mel_slotmap_remove(&g.sessions, h);
        res.status = sub;
        return res;
    }

    Mel_Speech_Session s = { h };
    if (mel_slotmap_alive(&g.sessions, h))
        mel_array_push(&g.active_sessions, s);
    res.value = s;
    res.status = warn ? (warn | MEL_SPEECH_WARNED) : MEL_SPEECH_OK;
    return res;
}

Mel_Speech_Status mel_speech_listen_stop(Mel_Speech_Session s)
{
    if (!g.initialized)
        return MEL_SPEECH_ERROR;
    Session_Slot* ss = (Session_Slot*)mel_slotmap_get(&g.sessions, s.h);
    if (!ss || ss->resolved)
    {
        mel_log_error("speech", "stop on dead or finished session");
        return MEL_SPEECH_ERROR;
    }
    if (ss->stopping)
        return MEL_SPEECH_OK;
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (prov && prov->desc.listen_stop)
    {
        ss->stopping = true;
        prov->desc.listen_stop(prov->desc.user, ss->stable_id, ss->token);
        return MEL_SPEECH_OK;
    }
    if (prov && prov->desc.listen_abort)
        prov->desc.listen_abort(prov->desc.user, ss->stable_id, ss->token);
    session_resolve(s.h, ss, MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED);
    return MEL_SPEECH_WARNED | MEL_SPEECH_WARN_STOP_SYNTHESIZED;
}

void mel_speech_listen_abort(Mel_Speech_Session s)
{
    if (!g.initialized)
        return;
    Session_Slot* ss = (Session_Slot*)mel_slotmap_get(&g.sessions, s.h);
    if (!ss || ss->resolved)
        return;
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (prov && prov->desc.listen_abort)
        prov->desc.listen_abort(prov->desc.user, ss->stable_id, ss->token);
    session_resolve(s.h, ss, MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED);
}

bool mel_speech_listening(Mel_Speech_Session s)
{
    Session_Slot* ss = g.initialized ? (Session_Slot*)mel_slotmap_get(&g.sessions, s.h) : NULL;
    return ss && !ss->resolved;
}

void* mel_speech_voice_native(Mel_Speech_Voice v)
{
    if (!g.initialized)
        return NULL;
    Voice_Slot* vs = voice_slot(v.h);
    if (!vs)
        return NULL;
    Provider_Entry* prov = provider_get(vs->provider_idx);
    return (prov && prov->desc.voice_native) ? prov->desc.voice_native(prov->desc.user, vs->stable_id) : NULL;
}

void* mel_speech_recognizer_native(Mel_Speech_Recognizer r)
{
    if (!g.initialized)
        return NULL;
    Recognizer_Slot* rs = recognizer_slot(r.h);
    if (!rs)
        return NULL;
    Provider_Entry* prov = provider_get(rs->provider_idx);
    return (prov && prov->desc.recognizer_native) ? prov->desc.recognizer_native(prov->desc.user, rs->stable_id) : NULL;
}
