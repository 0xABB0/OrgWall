#include <tts/provider.h>

#include <allocator/allocator.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

typedef struct
{
    Mel_Tts_Provider_Desc desc;
    u32                   generation;
    bool                  active;
} Provider_Entry;

typedef struct
{
    u32                provider_idx;
    u64                stable_id;
    str8               name;
    str8               language;
    str8               viseme_set;
    Mel_Tts_Voice_Caps caps;
} Voice_Slot;

typedef struct
{
    u64                stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Tts_Voice       voice;
    u32                 provider_idx;
    u64                 stable_id;
    u64                 token;
    Mel_Tts_On_Complete on_complete;
    Mel_Tts_On_Range    on_range;
    Mel_Tts_On_Viseme   on_viseme;
    Mel_Tts_On_Render   on_render;
    void*               user;
    bool                paused;
    _Atomic(bool)       resolved;
} Utterance_Slot;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;

    Mel_SlotMap voices;
    Mel_SlotMap utterances;

    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    Mel_Array(Mel_Tts_Utterance) active;

    u64 next_token;
    u32 provider_gen;
} Tts;

static Tts g;

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Voice_Slot* voice_slot(Mel_SlotMap_Handle h) { return (Voice_Slot*)mel_slotmap_get(&g.voices, h); }

static Utterance_Slot* utterance_slot(Mel_SlotMap_Handle h) { return (Utterance_Slot*)mel_slotmap_get(&g.utterances, h); }

static Reg_Entry* reg_find(u32 prov, u64 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && g.registry.items[i].stable_id == stable_id)
            return &g.registry.items[i];
    return NULL;
}

static void str8_owned_free(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(g.alloc, s->data);
    *s = STR8_EMPTY;
}

static void str8_owned_replace(str8* dst, str8 src)
{
    if (str8_equals(*dst, src))
        return;
    str8_owned_free(dst);
    *dst = str8_dup(src, g.alloc);
}

static void voice_teardown(Voice_Slot* s)
{
    str8_owned_free(&s->name);
    str8_owned_free(&s->language);
    str8_owned_free(&s->viseme_set);
}

static void active_remove(Mel_Tts_Utterance u)
{
    for (usize i = 0; i < g.active.count; i++)
    {
        if (g.active.items[i].h.index == u.h.index && g.active.items[i].h.generation == u.h.generation)
        {
            g.active.items[i] = g.active.items[g.active.count - 1];
            g.active.count--;
            return;
        }
    }
}

static void utterance_resolve(Mel_SlotMap_Handle h, Utterance_Slot* us, Mel_Tts_Status status, const Mel_Tts_Render* pcm)
{
    if (atomic_exchange(&us->resolved, true))
        return;
    Mel_Tts_Utterance   u = { h };
    Mel_Tts_On_Complete on_complete = us->on_complete;
    Mel_Tts_On_Render   on_render = us->on_render;
    void*               user = us->user;
    active_remove(u);
    mel_slotmap_remove(&g.utterances, h);
    if (on_render)
        on_render(u, pcm, status, user);
    else if (on_complete)
        on_complete(u, status, user);
}

static void core_on_range(void* token, Mel_Tts_Range range)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_from_ptr(token);
    Utterance_Slot*    us = utterance_slot(h);
    if (us && !atomic_load(&us->resolved) && us->on_range)
        us->on_range((Mel_Tts_Utterance){ h }, range, us->user);
}

static void core_on_viseme(void* token, Mel_Tts_Viseme viseme)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_from_ptr(token);
    Utterance_Slot*    us = utterance_slot(h);
    if (us && !atomic_load(&us->resolved) && us->on_viseme)
        us->on_viseme((Mel_Tts_Utterance){ h }, viseme, us->user);
}

static void core_on_done(void* token, Mel_Tts_Status status)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_from_ptr(token);
    Utterance_Slot*    us = utterance_slot(h);
    if (us)
        utterance_resolve(h, us, status, NULL);
}

static void core_on_render(void* token, const Mel_Tts_Render* pcm, Mel_Tts_Status status)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_from_ptr(token);
    Utterance_Slot*    us = utterance_slot(h);
    if (us)
        utterance_resolve(h, us, status, pcm);
}

static Mel_Tts_Sink utterance_sink(Mel_SlotMap_Handle h)
{
    return (Mel_Tts_Sink){
        .on_range = core_on_range,
        .on_viseme = core_on_viseme,
        .on_done = core_on_done,
        .on_render = core_on_render,
        .token = mel_slotmap_handle_to_ptr(h),
    };
}

Mel_Tts_Provider mel_tts_provider_register(const Mel_Tts_Provider_Desc* desc)
{
    assert(g.initialized);
    assert(desc != NULL);
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Tts_Provider){ .index = idx, .generation = e.generation };
}

void mel_tts_provider_unregister(Mel_Tts_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_tts_init(const Mel_Alloc* alloc)
{
    assert(!g.initialized);
    assert(alloc != NULL);
    if (g.initialized)
        return;
    g.alloc = alloc;
    mel_slotmap_init(&g.voices, g.alloc, .item_size = sizeof(Voice_Slot), .initial_capacity = 8);
    mel_slotmap_init(&g.utterances, g.alloc, .item_size = sizeof(Utterance_Slot), .initial_capacity = 8);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    mel_array_init(&g.active, g.alloc);
    g.next_token = 0;
    g.provider_gen = 0;
    g.initialized = true;
    mel_tts__register_host_providers();
    mel_tts_refresh();
}

void mel_tts_shutdown(void)
{
    assert(g.initialized);
    if (!g.initialized)
        return;

    while (g.active.count > 0)
        mel_tts_abort(g.active.items[g.active.count - 1]);

    for (usize i = 0; i < g.providers.count; i++)
        if (g.providers.items[i].active && g.providers.items[i].desc.shutdown)
            g.providers.items[i].desc.shutdown(g.providers.items[i].desc.user, g.alloc);

    for (usize i = 0; i < g.registry.count; i++)
    {
        Voice_Slot* s = voice_slot(g.registry.items[i].handle);
        if (s)
            voice_teardown(s);
    }

    mel_array_free(&g.active);
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.utterances);
    mel_slotmap_free(&g.voices);
    memset(&g, 0, sizeof g);
}

static void voice_lost(Mel_SlotMap_Handle h)
{
    Mel_Tts_Voice dead = { h };
    for (usize i = 0; i < g.active.count;)
    {
        Mel_Tts_Utterance u = g.active.items[i];
        Utterance_Slot*   us = utterance_slot(u.h);
        if (us && mel_tts_voice_equal(us->voice, dead))
        {
            utterance_resolve(u.h, us, MEL_TTS_ERROR | MEL_TTS_RESULT_LOST, NULL);
            i = 0;
        }
        else
            i++;
    }
}

static void voice_update(Voice_Slot* s, const Mel_Tts_Voice_Raw* raw)
{
    str8_owned_replace(&s->name, raw->name);
    str8_owned_replace(&s->language, raw->language);
    str8_owned_replace(&s->viseme_set, raw->viseme_set);
    s->caps = raw->caps;
}

u32 mel_tts_refresh(void)
{
    if (!g.initialized)
        return 0;

    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.registry.count; i++)
        mel_array_push(&seen, false);

    Mel_Array(Mel_Tts_Voice_Raw) tmp;
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
            Mel_Tts_Voice_Raw* raw = &tmp.items[i];
            Reg_Entry*         e = reg_find(pi, raw->stable_id);
            if (e)
            {
                seen.items[(usize)(e - g.registry.items)] = true;
                Voice_Slot* s = voice_slot(e->handle);
                if (s)
                    voice_update(s, raw);
                continue;
            }
            Voice_Slot slot;
            memset(&slot, 0, sizeof slot);
            slot.provider_idx = pi;
            slot.stable_id = raw->stable_id;
            slot.name = str8_dup(raw->name, g.alloc);
            slot.language = str8_dup(raw->language, g.alloc);
            slot.viseme_set = str8_dup(raw->viseme_set, g.alloc);
            slot.caps = raw->caps;

            Mel_SlotMap_Handle h = mel_slotmap_insert(&g.voices, &slot);
            Voice_Slot*        inserted = voice_slot(h);
            Reg_Entry          re = { .stable_id = raw->stable_id, .provider_idx = pi, .handle = h };
            mel_array_push(&g.registry, re);
            mel_array_push(&seen, true);
            mel_log_info("tts", "voice added: %.*s [%.*s] provider=%s id=%llu", (int)inserted->name.len, inserted->name.data, (int)inserted->language.len, inserted->language.data, pe->desc.name, (unsigned long long)raw->stable_id);
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
        voice_lost(h);
        Voice_Slot* s = voice_slot(h);
        if (s)
        {
            mel_log_info("tts", "voice removed: %.*s id=%llu", (int)s->name.len, s->name.data, (unsigned long long)s->stable_id);
            voice_teardown(s);
        }
        mel_slotmap_remove(&g.voices, h);
        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    mel_array_free(&seen);

    if (g.registry.count == 0)
        mel_log_warn("tts", "zero voices enumerated across %u provider(s)", (u32)g.providers.count);

    return (u32)g.registry.count;
}

u32 mel_tts_voice_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_tts_voice_list(Mel_Tts_Voice* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Tts_Voice){ g.registry.items[i].handle };
    return n;
}

Mel_Tts_Voice_Describe_Result mel_tts_voice_describe(Mel_Tts_Voice v)
{
    Mel_Tts_Voice_Describe_Result r = { 0 };
    Voice_Slot*                   s = g.initialized ? voice_slot(v.h) : NULL;
    if (!s)
    {
        mel_log_error("tts", "describe on dead voice handle {index=%u, gen=%u}", v.h.index, v.h.generation);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
        return r;
    }
    r.value.name = s->name;
    r.value.language = s->language;
    r.value.viseme_set = s->viseme_set;
    r.value.caps = s->caps;
    r.status = MEL_TTS_OK;
    return r;
}

bool mel_tts_voice_alive(Mel_Tts_Voice v) { return g.initialized && mel_slotmap_alive(&g.voices, v.h); }

bool mel_tts_voice_equal(Mel_Tts_Voice a, Mel_Tts_Voice b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

static Mel_Tts_Status lower(const Mel_Tts_Voice_Caps* caps, f32* rate, f32* pitch, f32* volume, bool* want_ranges, bool* want_visemes)
{
    Mel_Tts_Status warn = 0;
    if (*rate != 0.0f)
    {
        if (!caps->rate)
        {
            *rate = 0.0f;
            warn |= MEL_TTS_WARN_RATE_CLAMPED;
        }
        else if (caps->rate_max > 0.0f && *rate > caps->rate_max)
        {
            *rate = caps->rate_max;
            warn |= MEL_TTS_WARN_RATE_CLAMPED;
        }
        else if (caps->rate_min > 0.0f && *rate < caps->rate_min)
        {
            *rate = caps->rate_min;
            warn |= MEL_TTS_WARN_RATE_CLAMPED;
        }
    }
    if (*pitch != 0.0f && !caps->pitch)
    {
        *pitch = 0.0f;
        warn |= MEL_TTS_WARN_PITCH_DROPPED;
    }
    if (*volume != 0.0f && !caps->volume)
    {
        *volume = 0.0f;
        warn |= MEL_TTS_WARN_VOLUME_DROPPED;
    }
    if (*want_ranges && !caps->ranges)
    {
        *want_ranges = false;
        warn |= MEL_TTS_WARN_RANGES_DROPPED;
    }
    if (*want_visemes && !caps->visemes)
    {
        *want_visemes = false;
        warn |= MEL_TTS_WARN_VISEMES_DROPPED;
    }
    return warn;
}

Mel_Tts_Speak_Result mel_tts_speak_opt(Mel_Tts_Voice v, str8 text, Mel_Tts_Speak_Opt opt)
{
    Mel_Tts_Speak_Result r = { .value = MEL_TTS_UTTERANCE_NULL, .status = MEL_TTS_ERROR };
    if (!g.initialized)
    {
        mel_log_error("tts", "speak before init");
        return r;
    }
    Voice_Slot* vs = voice_slot(v.h);
    if (!vs)
    {
        mel_log_error("tts", "speak on dead voice handle {index=%u, gen=%u}", v.h.index, v.h.generation);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
        return r;
    }
    Provider_Entry* prov = provider_get(vs->provider_idx);
    if (!prov || !prov->desc.speak)
    {
        mel_log_error("tts", "voice %.*s has no speak provider", (int)vs->name.len, vs->name.data);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }
    if (str8_is_empty(text))
    {
        mel_log_error("tts", "speak with empty text");
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }
    if (opt.ssml && !vs->caps.ssml)
    {
        mel_log_error("tts", "voice %.*s does not support ssml; refusing to read markup as prose", (int)vs->name.len, vs->name.data);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }

    f32            rate = opt.rate;
    f32            pitch = opt.pitch;
    f32            volume = opt.volume;
    bool           want_ranges = opt.on_range != NULL;
    bool           want_visemes = opt.on_viseme != NULL;
    Mel_Tts_Status warn = lower(&vs->caps, &rate, &pitch, &volume, &want_ranges, &want_visemes);
    if (warn)
        mel_log_warn("tts", "speak lowered on %.*s: 0x%x", (int)vs->name.len, vs->name.data, warn);

    Utterance_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.voice = v;
    slot.provider_idx = vs->provider_idx;
    slot.stable_id = vs->stable_id;
    slot.token = ++g.next_token;
    slot.on_complete = opt.on_complete;
    slot.on_range = want_ranges ? opt.on_range : NULL;
    slot.on_viseme = want_visemes ? opt.on_viseme : NULL;
    slot.user = opt.user;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.utterances, &slot);

    Mel_Tts_Lowered lowered = { .text = text, .ssml = opt.ssml, .rate = rate, .pitch = pitch, .volume = volume, .want_ranges = want_ranges, .want_visemes = want_visemes, .caps = vs->caps };
    Mel_Tts_Status  sub = prov->desc.speak(prov->desc.user, vs->stable_id, slot.token, &lowered, utterance_sink(h));
    if (mel_tts_failed(sub))
    {
        mel_log_error("tts", "provider %s refused speak: 0x%x", prov->desc.name, sub);
        Utterance_Slot* us = utterance_slot(h);
        if (us && !atomic_exchange(&us->resolved, true))
            mel_slotmap_remove(&g.utterances, h);
        r.status = sub;
        return r;
    }

    Mel_Tts_Utterance u = { h };
    if (mel_slotmap_alive(&g.utterances, h))
        mel_array_push(&g.active, u);
    r.value = u;
    r.status = warn ? (warn | MEL_TTS_WARNED) : MEL_TTS_OK;
    return r;
}

Mel_Tts_Speak_Result mel_tts_render_opt(Mel_Tts_Voice v, str8 text, Mel_Tts_Render_Opt opt)
{
    Mel_Tts_Speak_Result r = { .value = MEL_TTS_UTTERANCE_NULL, .status = MEL_TTS_ERROR };
    if (!g.initialized)
    {
        mel_log_error("tts", "render before init");
        return r;
    }
    Voice_Slot* vs = voice_slot(v.h);
    if (!vs)
    {
        mel_log_error("tts", "render on dead voice handle {index=%u, gen=%u}", v.h.index, v.h.generation);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
        return r;
    }
    if (!opt.on_render)
    {
        mel_log_error("tts", "render without on_render; the terminal callback is mandatory");
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }
    if (!vs->caps.render)
    {
        mel_log_error("tts", "voice %.*s does not support render", (int)vs->name.len, vs->name.data);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }
    Provider_Entry* prov = provider_get(vs->provider_idx);
    if (!prov || !prov->desc.render)
    {
        mel_log_error("tts", "voice %.*s claims render but its provider has no render entry; provider bug", (int)vs->name.len, vs->name.data);
        assert(!prov || prov->desc.render);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }
    if (str8_is_empty(text))
    {
        mel_log_error("tts", "render with empty text");
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }
    if (opt.ssml && !vs->caps.ssml)
    {
        mel_log_error("tts", "voice %.*s does not support ssml; refusing to read markup as prose", (int)vs->name.len, vs->name.data);
        r.status = MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        return r;
    }

    f32            rate = opt.rate;
    f32            pitch = opt.pitch;
    f32            volume = opt.volume;
    bool           want_ranges = false;
    bool           want_visemes = false;
    Mel_Tts_Status warn = lower(&vs->caps, &rate, &pitch, &volume, &want_ranges, &want_visemes);
    if (warn)
        mel_log_warn("tts", "render lowered on %.*s: 0x%x", (int)vs->name.len, vs->name.data, warn);

    Utterance_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.voice = v;
    slot.provider_idx = vs->provider_idx;
    slot.stable_id = vs->stable_id;
    slot.token = ++g.next_token;
    slot.on_render = opt.on_render;
    slot.user = opt.user;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.utterances, &slot);

    Mel_Tts_Lowered lowered = { .text = text, .ssml = opt.ssml, .rate = rate, .pitch = pitch, .volume = volume, .caps = vs->caps };
    Mel_Tts_Status  sub = prov->desc.render(prov->desc.user, vs->stable_id, slot.token, &lowered, utterance_sink(h));
    if (mel_tts_failed(sub))
    {
        mel_log_error("tts", "provider %s refused render: 0x%x", prov->desc.name, sub);
        Utterance_Slot* us = utterance_slot(h);
        if (us && !atomic_exchange(&us->resolved, true))
            mel_slotmap_remove(&g.utterances, h);
        r.status = sub;
        return r;
    }

    Mel_Tts_Utterance u = { h };
    if (mel_slotmap_alive(&g.utterances, h))
        mel_array_push(&g.active, u);
    r.value = u;
    r.status = warn ? (warn | MEL_TTS_WARNED) : MEL_TTS_OK;
    return r;
}

Mel_Tts_Status mel_tts_pause(Mel_Tts_Utterance u)
{
    if (!g.initialized)
    {
        mel_log_error("tts", "pause before init");
        return MEL_TTS_ERROR;
    }
    Utterance_Slot* us = utterance_slot(u.h);
    if (!us || atomic_load(&us->resolved))
    {
        mel_log_error("tts", "pause on dead or resolved utterance {index=%u, gen=%u}", u.h.index, u.h.generation);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }
    if (us->paused)
        return MEL_TTS_OK;
    Voice_Slot* vs = voice_slot(us->voice.h);
    if (!vs)
    {
        mel_log_error("tts", "pause on utterance whose voice vanished");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }
    if (!vs->caps.can_pause)
    {
        mel_log_error("tts", "voice %.*s cannot pause", (int)vs->name.len, vs->name.data);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
    Provider_Entry* prov = provider_get(us->provider_idx);
    if (!prov || !prov->desc.pause)
    {
        mel_log_error("tts", "voice %.*s claims can_pause but its provider has no pause entry; provider bug", (int)vs->name.len, vs->name.data);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
    prov->desc.pause(prov->desc.user, us->stable_id, us->token);
    us->paused = true;
    return MEL_TTS_OK;
}

Mel_Tts_Status mel_tts_resume(Mel_Tts_Utterance u)
{
    if (!g.initialized)
    {
        mel_log_error("tts", "resume before init");
        return MEL_TTS_ERROR;
    }
    Utterance_Slot* us = utterance_slot(u.h);
    if (!us || atomic_load(&us->resolved))
    {
        mel_log_error("tts", "resume on dead or resolved utterance {index=%u, gen=%u}", u.h.index, u.h.generation);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }
    if (!us->paused)
        return MEL_TTS_OK;
    Provider_Entry* prov = provider_get(us->provider_idx);
    if (!prov || !prov->desc.resume)
    {
        mel_log_error("tts", "paused utterance has no resume provider entry; provider bug");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
    prov->desc.resume(prov->desc.user, us->stable_id, us->token);
    us->paused = false;
    return MEL_TTS_OK;
}

void mel_tts_abort(Mel_Tts_Utterance u)
{
    if (!g.initialized)
        return;
    Utterance_Slot* us = utterance_slot(u.h);
    if (!us || atomic_load(&us->resolved))
        return;
    Provider_Entry* prov = provider_get(us->provider_idx);
    if (prov && prov->desc.abort)
        prov->desc.abort(prov->desc.user, us->stable_id, us->token);
    utterance_resolve(u.h, us, MEL_TTS_OK | MEL_TTS_RESULT_ABORTED, NULL);
}

void mel_tts_abort_all(Mel_Tts_Voice v)
{
    if (!g.initialized)
        return;
    Mel_Array(Mel_Tts_Utterance) snap;
    mel_array_init(&snap, g.alloc);
    for (usize i = 0; i < g.active.count; i++)
    {
        Utterance_Slot* us = utterance_slot(g.active.items[i].h);
        if (us && mel_tts_voice_equal(us->voice, v))
            mel_array_push(&snap, g.active.items[i]);
    }
    for (usize i = 0; i < snap.count; i++)
        mel_tts_abort(snap.items[i]);
    mel_array_free(&snap);
}

bool mel_tts_speaking(Mel_Tts_Utterance u)
{
    Utterance_Slot* us = g.initialized ? utterance_slot(u.h) : NULL;
    return us && !atomic_load(&us->resolved) && !us->paused;
}

bool mel_tts_paused(Mel_Tts_Utterance u)
{
    Utterance_Slot* us = g.initialized ? utterance_slot(u.h) : NULL;
    return us && !atomic_load(&us->resolved) && us->paused;
}

void* mel_tts_voice_native(Mel_Tts_Voice v)
{
    if (!g.initialized)
        return NULL;
    Voice_Slot* vs = voice_slot(v.h);
    if (!vs)
        return NULL;
    Provider_Entry* prov = provider_get(vs->provider_idx);
    return (prov && prov->desc.voice_native) ? prov->desc.voice_native(prov->desc.user, vs->stable_id) : NULL;
}
