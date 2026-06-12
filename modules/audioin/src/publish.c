#include "audioin_internal.h"

#include <allocator/allocator.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <pcm/ring.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOIN_PUBLISH_SCRATCH_FRAMES 1024u

typedef struct
{
    u32              count;
    Mel_AudioIn_Sink sinks[];
} Sink_List;

typedef struct
{
    str8             stable_id;
    str8             name;
    u32              channels;
    u32              samplerate;
    Mel_Pcm_Ring*    ring;
    f32*             scratch;
    u32              scratch_frames;
    const Mel_Alloc* alloc;
    _Atomic(void*)   sinks;
    Mel_Array(void*) garbage;
} Pub_Slot;

typedef struct
{
    bool             registered;
    const Mel_Alloc* alloc;
    Mel_SlotMap      pubs;
    Mel_Array(Mel_SlotMap_Handle) order;
    Mel_AudioIn_Provider provider;
    u32                  seq;
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

static void pub_sinks_swap(Pub_Slot* p, Sink_List* nl)
{
    void* old = atomic_exchange_explicit(&p->sinks, nl, memory_order_acq_rel);
    if (old)
        mel_array_push(&p->garbage, old);
}

static void pub_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < pg.order.count; i++)
    {
        Pub_Slot* p = pub_get(pg.order.items[i]);
        if (!p)
            continue;
        Mel_AudioIn_Raw raw = {
            .stable_id = p->stable_id,
            .name = p->name,
            .kind = &mel_audioin_virtual,
            .channels = p->channels,
            .samplerate = p->samplerate,
            .samplerates = &p->samplerate,
            .samplerate_count = 1,
            .caps = { .gain = false },
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

static Mel_AudioIn_Status pub_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Granted* granted)
{
    MEL_UNUSED(user);
    assert(granted != NULL);
    Pub_Slot* p = pub_find(stable_id);
    if (!p)
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;

    *granted = (Mel_AudioIn_Granted){ 0 };
    if (opt.processing.echo_cancellation || opt.processing.noise_suppression || opt.processing.auto_gain || opt.exclusive)
        mel_log_info("audioin", "published input '%.*s': processing/exclusive requests lower to none", (int)p->name.len, p->name.data);

    Sink_List* cur = atomic_load_explicit(&p->sinks, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Sink_List* nl = mel_alloc(p->alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
    if (!nl)
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    for (u32 i = 0; i < count; i++)
        nl->sinks[i] = cur->sinks[i];
    nl->sinks[count] = sink;
    nl->count = count + 1u;
    pub_sinks_swap(p, nl);
    return MEL_AUDIOIN_OK;
}

static void pub_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Pub_Slot* p = pub_find(stable_id);
    if (!p)
        return;

    Sink_List* cur = atomic_load_explicit(&p->sinks, memory_order_acquire);
    if (!cur || cur->count == 0)
        return;

    Sink_List* nl = mel_alloc(p->alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * (usize)cur->count);
    if (!nl)
        return;
    u32 kept = 0;
    for (u32 i = 0; i < cur->count; i++)
        if (cur->sinks[i].token != token)
            nl->sinks[kept++] = cur->sinks[i];
    nl->count = kept;
    pub_sinks_swap(p, nl);
}

static void pub_release(Pub_Slot* p, bool lost)
{
    Sink_List* sl = atomic_exchange_explicit(&p->sinks, NULL, memory_order_acq_rel);
    if (sl)
    {
        if (lost)
            for (u32 i = 0; i < sl->count; i++)
                if (sl->sinks[i].on_lost)
                    sl->sinks[i].on_lost(sl->sinks[i].token);
        mel_dealloc(p->alloc, sl);
    }
    for (usize i = 0; i < p->garbage.count; i++)
        mel_dealloc(p->alloc, p->garbage.items[i]);
    mel_array_free(&p->garbage);

    mel_pcm_ring_destroy(p->ring);
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
        mel_log_warn("audioin", "published input '%.*s' still live at shutdown; releasing", (int)p->name.len, p->name.data);
        pub_release(p, true);
    }
    mel_array_free(&pg.order);
    mel_slotmap_free(&pg.pubs);
    memset(&pg, 0, sizeof pg);
}

static const Mel_AudioIn_Provider_Desc PUBLISH_DESC = {
    .name = "publish",
    .enumerate = pub_enumerate,
    .default_id = pub_default_id,
    .open = pub_open,
    .close = pub_close,
    .shutdown = pub_provider_shutdown,
};

void mel_audioin__publish_register_provider(const Mel_Alloc* alloc)
{
    pg.alloc = alloc;
    mel_slotmap_init(&pg.pubs, alloc, .item_size = sizeof(Pub_Slot*), .initial_capacity = 2);
    mel_array_init(&pg.order, alloc);
    pg.seq = 0;
    pg.provider = mel_audioin_provider_register(&PUBLISH_DESC);
    pg.registered = true;
}

Mel_AudioIn_Publish_Result mel_audioin_publish(const Mel_Alloc* a, Mel_AudioIn_Publish_Opt opt)
{
    Mel_AudioIn_Publish_Result r = { .published = MEL_AUDIOIN_PUBLISHED_NULL, .device = MEL_AUDIOIN_NULL };
    assert(a != NULL);
    assert(opt.name.len > 0);
    assert(opt.channels > 0);
    assert(opt.samplerate > 0);
    assert(opt.ring_capacity_frames > 0);
    if (!pg.registered || !a || opt.name.len == 0 || opt.channels == 0 || opt.samplerate == 0 || opt.ring_capacity_frames == 0)
    {
        mel_log_error("audioin", "publish with invalid options or before init");
        r.status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        return r;
    }

    Pub_Slot* p = mel_alloc_type(a, Pub_Slot);
    if (!p)
    {
        r.status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        return r;
    }
    memset(p, 0, sizeof *p);
    p->alloc = a;
    p->channels = opt.channels;
    p->samplerate = opt.samplerate;
    p->ring = mel_pcm_ring_create(a, opt.channels, opt.ring_capacity_frames);
    p->scratch_frames = opt.ring_capacity_frames < MEL_AUDIOIN_PUBLISH_SCRATCH_FRAMES ? opt.ring_capacity_frames : MEL_AUDIOIN_PUBLISH_SCRATCH_FRAMES;
    p->scratch = mel_alloc(a, sizeof(f32) * (usize)p->scratch_frames * opt.channels);
    p->name = str8_dup(opt.name, a);
    p->stable_id = str8_fmt(a, "publish:%.*s#%u", (int)opt.name.len, opt.name.data, ++pg.seq);
    mel_array_init(&p->garbage, a);
    atomic_store_explicit(&p->sinks, NULL, memory_order_relaxed);

    if (!p->ring || !p->scratch)
    {
        pub_release(p, false);
        r.status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        return r;
    }

    Mel_SlotMap_Handle h = mel_slotmap_insert(&pg.pubs, &p);
    mel_array_push(&pg.order, h);

    mel_audioin_provider_notify(pg.provider);

    r.published = (Mel_AudioIn_Published){ h };
    r.device = mel_audioin_find(p->stable_id);
    r.status = MEL_AUDIOIN_WARNED | MEL_AUDIOIN_WARN_LOCAL_ONLY;
    mel_log_info("audioin", "published input '%.*s' (%u ch @ %u Hz); local registry only on this platform", (int)p->name.len, p->name.data, p->channels, p->samplerate);
    return r;
}

u32 mel_audioin_publish_feed(Mel_AudioIn_Published pub, const f32* interleaved, u32 frames)
{
    assert(interleaved != NULL);
    Pub_Slot* p = pub_get(pub.handle);
    if (!p)
    {
        mel_log_error("audioin", "feed on dead published handle");
        return 0;
    }

    u32 accepted = mel_pcm_ring_write(p->ring, interleaved, frames);

    Sink_List* sl = atomic_load_explicit(&p->sinks, memory_order_acquire);
    if (sl && sl->count > 0)
    {
        u32 got;
        while ((got = mel_pcm_ring_read(p->ring, p->scratch, p->scratch_frames)) > 0)
            for (u32 i = 0; i < sl->count; i++)
                if (sl->sinks[i].on_frames)
                    sl->sinks[i].on_frames(sl->sinks[i].token, p->scratch, got, p->samplerate, p->channels, 0);
    }
    return accepted;
}

bool mel_audioin_publish_os_visible(Mel_AudioIn_Published pub)
{
    MEL_UNUSED(pub);
    return false;
}

void mel_audioin_unpublish(Mel_AudioIn_Published pub)
{
    Pub_Slot* p = pub_get(pub.handle);
    if (!p)
    {
        mel_log_error("audioin", "unpublish on dead published handle");
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
    mel_audioin_provider_notify(pg.provider);
}
