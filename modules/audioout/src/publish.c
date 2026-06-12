#include "audioout_internal.h"

#include <allocator/allocator.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOOUT_PUBLISH_SCRATCH_FRAMES 1024u

typedef struct
{
    Mel_AudioOut_Source src;
    bool                started;
} Pub_Open;

typedef struct
{
    u32      count;
    Pub_Open opens[];
} Open_List;

typedef struct
{
    str8             stable_id;
    str8             name;
    u32              channels;
    u32              samplerate;
    f32*             scratch;
    u32              scratch_frames;
    const Mel_Alloc* alloc;
    _Atomic(void*)   opens;
    Mel_Array(void*) garbage;
} Pub_Slot;

typedef struct
{
    bool             registered;
    const Mel_Alloc* alloc;
    Mel_SlotMap      pubs;
    Mel_Array(Mel_SlotMap_Handle) order;
    Mel_AudioOut_Provider provider;
    u32                   seq;
} Publish_State;

static Publish_State pg;

static Pub_Slot* pub_get(Mel_SlotMap_Handle h)
{
    Pub_Slot** pp = (Pub_Slot**)mel_slotmap_get(&pg.pubs, h);
    return pp ? *pp : NULL;
}

static Pub_Slot* pub_find(str8 stable_id)
{
    for (usize i = 0; i < pg.order.count; i++)
    {
        Pub_Slot* p = pub_get(pg.order.items[i]);
        if (p && str8_equals(p->stable_id, stable_id))
            return p;
    }
    return NULL;
}

static void pub_opens_swap(Pub_Slot* p, Open_List* nl)
{
    void* old = atomic_exchange_explicit(&p->opens, nl, memory_order_acq_rel);
    if (old)
        mel_array_push(&p->garbage, old);
}

static Open_List* pub_opens_clone(Pub_Slot* p, u32 extra)
{
    Open_List* cur = atomic_load_explicit(&p->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(p->alloc, sizeof(Open_List) + sizeof(Pub_Open) * ((usize)count + extra));
    if (!nl)
        return NULL;
    for (u32 i = 0; i < count; i++)
        nl->opens[i] = cur->opens[i];
    nl->count = count;
    return nl;
}

static void pub_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < pg.order.count; i++)
    {
        Pub_Slot* p = pub_get(pg.order.items[i]);
        if (!p)
            continue;
        Mel_AudioOut_Raw raw = {
            .stable_id = p->stable_id,
            .name = p->name,
            .kind = &mel_audioout_virtual,
            .channels = p->channels,
            .samplerate = p->samplerate,
            .samplerates = &p->samplerate,
            .samplerate_count = 1,
            .caps = { .volume = false, .mute = false },
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 pub_default_id(void* user)
{
    MEL_UNUSED(user);
    return STR8_EMPTY;
}

static Mel_AudioOut_Status pub_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Open_Opt opt, Mel_AudioOut_Granted* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    MEL_UNUSED(req);
    MEL_UNUSED(opt);
    assert(granted != NULL);
    assert(src.pull != NULL);
    Pub_Slot* p = pub_find(stable_id);
    if (!p)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;

    Open_List* nl = pub_opens_clone(p, 1);
    if (!nl)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    nl->opens[nl->count] = (Pub_Open){ .src = src, .started = false };
    nl->count++;
    pub_opens_swap(p, nl);

    granted->format.samplerate = p->samplerate;
    granted->format.channels = p->channels;
    granted->format.block_frames = p->scratch_frames;
    granted->exclusive = false;
    granted->os_timestamps = false;
    granted->latency_frames = 0;
    return MEL_AUDIOOUT_OK;
}

static void pub_set_started(str8 stable_id, void* token, bool started)
{
    Pub_Slot* p = pub_find(stable_id);
    if (!p)
        return;
    Open_List* nl = pub_opens_clone(p, 0);
    if (!nl)
        return;
    for (u32 i = 0; i < nl->count; i++)
        if (nl->opens[i].src.token == token)
            nl->opens[i].started = started;
    pub_opens_swap(p, nl);
}

static void pub_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    pub_set_started(stable_id, token, true);
}

static void pub_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    pub_set_started(stable_id, token, false);
}

static void pub_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Pub_Slot* p = pub_find(stable_id);
    if (!p)
        return;
    Open_List* cur = atomic_load_explicit(&p->opens, memory_order_acquire);
    if (!cur || cur->count == 0)
        return;
    Open_List* nl = mel_alloc(p->alloc, sizeof(Open_List) + sizeof(Pub_Open) * (usize)cur->count);
    if (!nl)
        return;
    u32 kept = 0;
    for (u32 i = 0; i < cur->count; i++)
        if (cur->opens[i].src.token != token)
            nl->opens[kept++] = cur->opens[i];
    nl->count = kept;
    pub_opens_swap(p, nl);
}

static void pub_release(Pub_Slot* p, bool lost)
{
    Open_List* ol = atomic_exchange_explicit(&p->opens, NULL, memory_order_acq_rel);
    if (ol)
    {
        if (lost)
            for (u32 i = 0; i < ol->count; i++)
                if (ol->opens[i].src.on_lost)
                    ol->opens[i].src.on_lost(ol->opens[i].src.token);
        mel_dealloc(p->alloc, ol);
    }
    for (usize i = 0; i < p->garbage.count; i++)
        mel_dealloc(p->alloc, p->garbage.items[i]);
    mel_array_free(&p->garbage);

    mel_dealloc(p->alloc, p->scratch);
    if (p->stable_id.data)
        mel_dealloc(p->alloc, p->stable_id.data);
    if (p->name.data)
        mel_dealloc(p->alloc, p->name.data);
    mel_dealloc(p->alloc, p);
}

static void pub_provider_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    for (usize i = 0; i < pg.order.count; i++)
    {
        Pub_Slot* p = pub_get(pg.order.items[i]);
        if (!p)
            continue;
        mel_log_warn("audioout", "published output '%.*s' still live at shutdown; releasing", (int)p->name.len, p->name.data);
        pub_release(p, true);
    }
    mel_array_free(&pg.order);
    mel_slotmap_free(&pg.pubs);
    memset(&pg, 0, sizeof pg);
}

static const Mel_AudioOut_Provider_Desc PUBLISH_DESC = {
    .name = "publish",
    .enumerate = pub_enumerate,
    .default_id = pub_default_id,
    .open = pub_open,
    .start = pub_start,
    .stop = pub_stop,
    .close = pub_close,
    .shutdown = pub_provider_shutdown,
};

void mel_audioout__publish_register_provider(const Mel_Alloc* alloc)
{
    pg.alloc = alloc;
    mel_slotmap_init(&pg.pubs, alloc, .item_size = sizeof(Pub_Slot*), .initial_capacity = 2);
    mel_array_init(&pg.order, alloc);
    pg.seq = 0;
    pg.provider = mel_audioout_provider_register(&PUBLISH_DESC);
    pg.registered = true;
}

Mel_AudioOut_Publish_Result mel_audioout_publish(const Mel_Alloc* a, Mel_AudioOut_Publish_Opt opt)
{
    Mel_AudioOut_Publish_Result r = { .published = MEL_AUDIOOUT_PUBLISHED_NULL, .device = MEL_AUDIOOUT_NULL };
    assert(a != NULL);
    assert(opt.name.len > 0);
    assert(opt.channels > 0);
    assert(opt.samplerate > 0);
    assert(opt.ring_capacity_frames > 0);
    if (!pg.registered || !a || opt.name.len == 0 || opt.channels == 0 || opt.samplerate == 0 || opt.ring_capacity_frames == 0)
    {
        mel_log_error("audioout", "publish with invalid options or before init");
        r.status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        return r;
    }

    Pub_Slot* p = mel_alloc_type(a, Pub_Slot);
    if (!p)
    {
        r.status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        return r;
    }
    memset(p, 0, sizeof *p);
    p->alloc = a;
    p->channels = opt.channels;
    p->samplerate = opt.samplerate;
    p->scratch_frames = opt.ring_capacity_frames < MEL_AUDIOOUT_PUBLISH_SCRATCH_FRAMES ? opt.ring_capacity_frames : MEL_AUDIOOUT_PUBLISH_SCRATCH_FRAMES;
    p->scratch = mel_alloc(a, sizeof(f32) * (usize)p->scratch_frames * opt.channels);
    p->name = str8_dup(opt.name, a);
    p->stable_id = str8_fmt(a, "publish:%.*s#%u", (int)opt.name.len, opt.name.data, ++pg.seq);
    mel_array_init(&p->garbage, a);
    atomic_store_explicit(&p->opens, NULL, memory_order_relaxed);

    if (!p->scratch)
    {
        pub_release(p, false);
        r.status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        return r;
    }

    Mel_SlotMap_Handle h = mel_slotmap_insert(&pg.pubs, &p);
    mel_array_push(&pg.order, h);

    mel_audioout_provider_notify(pg.provider);

    r.published = (Mel_AudioOut_Published){ h };
    r.device = mel_audioout_find(p->stable_id);
    r.status = MEL_AUDIOOUT_WARNED | MEL_AUDIOOUT_WARN_LOCAL_ONLY;
    mel_log_info("audioout", "published output '%.*s' (%u ch @ %u Hz); local registry only on this platform", (int)p->name.len, p->name.data, p->channels, p->samplerate);
    return r;
}

u32 mel_audioout_publish_read(Mel_AudioOut_Published pub, f32* interleaved_dst, u32 max_frames)
{
    assert(interleaved_dst != NULL);
    Pub_Slot* p = pub_get(pub.handle);
    if (!p)
    {
        mel_log_error("audioout", "publish_read on dead published handle");
        return 0;
    }

    u32 frames = max_frames < p->scratch_frames ? max_frames : p->scratch_frames;
    if (frames == 0)
        return 0;

    Open_List* ol = atomic_load_explicit(&p->opens, memory_order_acquire);
    if (!ol || ol->count == 0)
        return 0;

    u32 channels = p->channels;
    u32 produced = 0;
    memset(interleaved_dst, 0, sizeof(f32) * (usize)frames * channels);

    for (u32 i = 0; i < ol->count; i++)
    {
        if (!ol->opens[i].started)
            continue;
        u32 got = ol->opens[i].src.pull(ol->opens[i].src.token, p->scratch, frames);
        if (got > frames)
            got = frames;
        for (usize sample = 0; sample < (usize)got * channels; sample++)
            interleaved_dst[sample] += p->scratch[sample];
        if (got > produced)
            produced = got;
    }
    return produced;
}

bool mel_audioout_publish_os_visible(Mel_AudioOut_Published pub)
{
    MEL_UNUSED(pub);
    return false;
}

void mel_audioout_unpublish(Mel_AudioOut_Published pub)
{
    Pub_Slot* p = pub_get(pub.handle);
    if (!p)
    {
        mel_log_error("audioout", "unpublish on dead published handle");
        return;
    }

    for (usize i = 0; i < pg.order.count; i++)
    {
        if (pg.order.items[i].index == pub.handle.index && pg.order.items[i].generation == pub.handle.generation)
        {
            pg.order.items[i] = pg.order.items[pg.order.count - 1];
            pg.order.count--;
            break;
        }
    }
    mel_slotmap_remove(&pg.pubs, pub.handle);
    pub_release(p, true);
    mel_audioout_provider_notify(pg.provider);
}
