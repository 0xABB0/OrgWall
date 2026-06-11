#include <notification/notification.h>
#include <notification/events.h>
#include <notification/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <event/event.h>
#include <future/future.h>
#include <log/log.h>

#include <string.h>

#include "notification_internal.h"

#define MEL_NOTIF_EVENTS_CAP 128

const mel_notif_auth mel_notif_auth_granted = { "granted", true };
const mel_notif_auth mel_notif_auth_provisional = { "provisional", true };
const mel_notif_auth mel_notif_auth_denied = { "denied", false };
const mel_notif_auth mel_notif_auth_not_determined = { "not_determined", false };

const char* mel_notif_auth_name(const mel_notif_auth* a) { return a != NULL ? a->name : "null"; }
bool        mel_notif_auth_is_granted(const mel_notif_auth* a) { return a != NULL && a->granted; }

typedef struct
{
    Mel_Notif_Provider_Desc desc;
    u32                     generation;
    bool                    active;
} Provider_Entry;

typedef struct
{
    Mel_Future       future;
    const Mel_Alloc* alloc;
    bool             resolved;
} Auth_Job;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;

    Mel_SlotMap notifs;

    Mel_Array(Provider_Entry) providers;
    u32 provider_gen;
    i32 active_provider;

    Mel_Array(str8) channels;
    bool default_channel;

    Mel_Event*    events;
    Mel_Event_Sub poll_sub;

    Mel_Array(void*) blobs;
    u32 blob_head;

    const mel_notif_auth* auth;
    Auth_Job*             pending_auth;
} Notif_Reg;

static Notif_Reg g;

Notif_Slot*      mel_notif__slot(Mel_SlotMap_Handle h) { return (Notif_Slot*)mel_slotmap_get(&g.notifs, h); }
const Mel_Alloc* mel_notif__alloc(void) { return g.alloc; }

static Provider_Entry* active_provider(void)
{
    if (g.active_provider >= 0)
    {
        Provider_Entry* pe = &g.providers.items[g.active_provider];
        if (pe->active)
            return pe;
        g.active_provider = -1;
    }
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* pe = &g.providers.items[i];
        if (!pe->active)
            continue;
        if (pe->desc.supported == NULL || pe->desc.supported(pe->desc.user))
        {
            g.active_provider = (i32)i;
            return pe;
        }
    }
    return NULL;
}

Mel_Notif_Provider_Desc* mel_notif__active_provider_desc(void)
{
    Provider_Entry* pe = active_provider();
    return pe != NULL ? &pe->desc : NULL;
}

static Mel_Notif_Caps provider_caps(Provider_Entry* pe)
{
    return (pe != NULL && pe->desc.caps != NULL) ? pe->desc.caps(pe->desc.user) : 0;
}

static str8 str_dup(str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return STR8_EMPTY;
    return str8_dup_alloc(s, g.alloc);
}

static void str_free(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(g.alloc, s->data);
    *s = STR8_EMPTY;
}

static Mel_Notif_Image image_dup(Mel_Notif_Image src, Mel_Notif_Status* warn)
{
    Mel_Notif_Image out = { 0 };
    out.path = str_dup(src.path);
    if (src.rgba != NULL && src.width > 0 && src.height > 0)
    {
        usize bytes = (usize)src.width * (usize)src.height * 4u;
        u8*   buf = mel_alloc(g.alloc, bytes);
        if (buf != NULL)
        {
            memcpy(buf, src.rgba, bytes);
            out.rgba = buf;
            out.width = src.width;
            out.height = src.height;
        }
        else if (warn != NULL)
            *warn |= MEL_NOTIF_ERR_BACKEND_FAIL;
    }
    return out;
}

static void image_free(Mel_Notif_Image* img)
{
    if (img->rgba != NULL)
        mel_dealloc(g.alloc, (void*)img->rgba);
    str_free(&img->path);
    *img = (Mel_Notif_Image){ 0 };
}

static void slot_free_content(Notif_Slot* ns)
{
    str_free(&ns->content.title);
    str_free(&ns->content.subtitle);
    str_free(&ns->content.body);
    str_free(&ns->content.channel);
    str_free(&ns->content.group);
    str_free(&ns->content.sound_path);
    str_free(&ns->content.payload);
    image_free(&ns->content.icon);
    image_free(&ns->content.attachment);
    for (u32 i = 0; i < ns->content.action_count; i++)
    {
        str_free(&ns->actions[i].id);
        str_free(&ns->actions[i].label);
        str_free(&ns->actions[i].input_placeholder);
    }
    if (ns->actions != NULL)
        mel_dealloc(g.alloc, ns->actions);
    ns->actions = NULL;
    ns->content = (Mel_Notif_Content){ 0 };
}

static Mel_Notif_Status slot_dup_content(Notif_Slot* ns, const Mel_Notif_Content* src)
{
    Mel_Notif_Status warn = 0;
    ns->content = (Mel_Notif_Content){ 0 };
    ns->content.title = str_dup(src->title);
    ns->content.subtitle = str_dup(src->subtitle);
    ns->content.body = str_dup(src->body);
    ns->content.channel = str_dup(src->channel);
    ns->content.group = str_dup(src->group);
    ns->content.sound_path = str_dup(src->sound_path);
    ns->content.payload = str_dup(src->payload);
    ns->content.icon = image_dup(src->icon, &warn);
    ns->content.attachment = image_dup(src->attachment, &warn);
    ns->content.progress = src->progress;
    ns->content.silent = src->silent;
    ns->content.has_badge = src->has_badge;
    ns->content.badge = src->badge;
    if (src->actions != NULL && src->action_count > 0)
    {
        ns->actions = mel_alloc(g.alloc, sizeof(Mel_Notif_Action) * src->action_count);
        if (ns->actions == NULL)
            return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
        for (u32 i = 0; i < src->action_count; i++)
        {
            ns->actions[i] = (Mel_Notif_Action){
                .id = str_dup(src->actions[i].id),
                .label = str_dup(src->actions[i].label),
                .flags = src->actions[i].flags,
                .input_placeholder = str_dup(src->actions[i].input_placeholder),
            };
        }
        ns->content.actions = ns->actions;
        ns->content.action_count = src->action_count;
    }
    return warn;
}

static void overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("notification", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void blob_push(void* buf)
{
    if (buf == NULL)
        return;
    if (g.blobs.count < MEL_NOTIF_EVENTS_CAP)
    {
        mel_array_push(&g.blobs, buf);
        return;
    }
    mel_dealloc(g.alloc, g.blobs.items[g.blob_head]);
    g.blobs.items[g.blob_head] = buf;
    g.blob_head = (g.blob_head + 1) % MEL_NOTIF_EVENTS_CAP;
}

static bool blob_views(const str8* in, str8* out, u32 n)
{
    usize total = 0;
    for (u32 i = 0; i < n; i++)
        total += in[i].len;
    for (u32 i = 0; i < n; i++)
        out[i] = STR8_EMPTY;
    if (total == 0)
        return true;
    u8* buf = mel_alloc(g.alloc, total);
    if (buf == NULL)
        return false;
    usize off = 0;
    for (u32 i = 0; i < n; i++)
    {
        if (in[i].len == 0 || in[i].data == NULL)
            continue;
        memcpy(buf + off, in[i].data, in[i].len);
        out[i] = (str8){ buf + off, in[i].len };
        off += in[i].len;
    }
    blob_push(buf);
    return true;
}

static void fire_event(Mel_Notif_Event ev)
{
    if (g.events != NULL)
        mel_event_fire(g.events, &ev);
}

Mel_Notif_Provider mel_notif_provider_register(const Mel_Notif_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Notif_Provider){ .index = idx, .generation = e.generation };
}

void mel_notif_provider_unregister(Mel_Notif_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
    {
        g.providers.items[p.index].active = false;
        if (g.active_provider == (i32)p.index)
            g.active_provider = -1;
    }
}

void mel_notif__force_provider(Mel_Notif_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation && g.providers.items[p.index].active)
        g.active_provider = (i32)p.index;
}

void mel_notif__init_bare(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.exec = exec;
    g.active_provider = -1;
    g.provider_gen = 0;
    g.auth = &mel_notif_auth_not_determined;
    mel_slotmap_init(&g.notifs, g.alloc, .item_size = sizeof(Notif_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.channels, g.alloc);
    mel_array_init(&g.blobs, g.alloc);
    g.blob_head = 0;
    g.events = mel_event_create(g.alloc, sizeof(Mel_Notif_Event), MEL_NOTIF_EVENTS_CAP, mel_event_policy_latest(overflow_report, NULL));
    g.poll_sub = g.events != NULL ? mel_event_subscribe_pull(g.events, NULL) : MEL_EVENT_SUB_NULL;
    g.initialized = true;
}

void mel_notif_init(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    mel_notif__init_bare(alloc, exec);
    mel_notif__register_host_providers();
}

static void notif_destroy_internal(Mel_SlotMap_Handle h, bool cancel_backend)
{
    Notif_Slot* ns = mel_notif__slot(h);
    if (ns == NULL)
        return;
    if (cancel_backend)
    {
        Provider_Entry* prov = active_provider();
        if (prov != NULL && prov->desc.cancel != NULL)
            prov->desc.cancel(prov->desc.user, mel_slotmap_handle_pack64(h));
    }
    slot_free_content(ns);
    mel_slotmap_remove(&g.notifs, h);
}

void mel_notif_shutdown(void)
{
    if (!g.initialized)
        return;
    if (g.pending_auth != NULL && !g.pending_auth->resolved)
    {
        g.pending_auth->resolved = true;
        mel_future_cancel(&g.pending_auth->future);
    }
    g.pending_auth = NULL;
    Provider_Entry* prov = active_provider();
    if (prov != NULL && prov->desc.shutdown != NULL)
        prov->desc.shutdown(prov->desc.user);
    Mel_Array(Mel_SlotMap_Handle) live;
    mel_array_init(&live, g.alloc);
    for (u32 i = 0; i < g.notifs.slot_count; i++)
    {
        Mel_SlotMap_Handle h = mel_slotmap_handle_make(i, g.notifs.slots[i].generation);
        if (mel_slotmap_alive(&g.notifs, h))
            mel_array_push(&live, h);
    }
    for (usize i = 0; i < live.count; i++)
        notif_destroy_internal(live.items[i], false);
    mel_array_free(&live);
    for (usize i = 0; i < g.channels.count; i++)
        str_free(&g.channels.items[i]);
    mel_array_free(&g.channels);
    for (usize i = 0; i < g.blobs.count; i++)
        mel_dealloc(g.alloc, g.blobs.items[i]);
    mel_array_free(&g.blobs);
    if (g.events != NULL)
        mel_event_unsubscribe(g.events, g.poll_sub);
    mel_event_destroy(g.events);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.notifs);
    memset(&g, 0, sizeof g);
}

bool mel_notif_supported(void)
{
    if (!g.initialized)
        return false;
    return active_provider() != NULL;
}

Mel_Notif_Caps mel_notif_caps(void)
{
    if (!g.initialized)
        return 0;
    return provider_caps(active_provider());
}

const mel_notif_auth* mel_notif_authorization(void)
{
    if (!g.initialized)
        return &mel_notif_auth_not_determined;
    Provider_Entry* prov = active_provider();
    if (prov == NULL)
        return &mel_notif_auth_not_determined;
    if (prov->desc.authorization != NULL)
    {
        const mel_notif_auth* a = prov->desc.authorization(prov->desc.user);
        if (a != NULL)
            return a;
    }
    return (provider_caps(prov) & MEL_NOTIF_CAP_AUTH) != 0 ? &mel_notif_auth_not_determined : &mel_notif_auth_granted;
}

static void auth_resolve(Auth_Job* j, const mel_notif_auth* auth)
{
    if (j == NULL || j->resolved)
        return;
    j->resolved = true;
    Mel_Future_Status fs = mel_notif_auth_is_granted(auth) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR;
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_notif__dispatch_auth_changed(auth);
    mel_future_resolve(&j->future, (void*)auth, fs);
}

static void core_on_auth(void* token, const mel_notif_auth* auth)
{
    MEL_UNUSED(token);
    auth_resolve(g.pending_auth, auth);
}

Mel_Future* mel_notif_authorize(const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    const Mel_Alloc* alloc = a ? a : g.alloc;
    Auth_Job*        j = mel_alloc_type(alloc, Auth_Job);
    if (j == NULL)
        return NULL;
    memset(j, 0, sizeof *j);
    j->alloc = alloc;
    mel_future_init(&j->future, NULL, alloc);
    g.pending_auth = j;

    Provider_Entry* prov = active_provider();
    if (prov == NULL || prov->desc.authorize == NULL)
    {
        auth_resolve(j, mel_notif_authorization());
        return &j->future;
    }
    Mel_Notif_Sink sink = { .on_auth = core_on_auth, .token = NULL };
    prov->desc.authorize(prov->desc.user, sink);
    return &j->future;
}

const mel_notif_auth* mel_notif_future_auth(const Mel_Future* f)
{
    const mel_notif_auth* a = f != NULL ? (const mel_notif_auth*)mel_future_value((Mel_Future*)f) : NULL;
    return a != NULL ? a : &mel_notif_auth_not_determined;
}

static bool channel_registered(str8 id)
{
    for (usize i = 0; i < g.channels.count; i++)
        if (str8_equals(g.channels.items[i], id))
            return true;
    return false;
}

Mel_Notif_Status mel_notif_channel_register(Mel_Notif_Channel_Opt opt)
{
    if (!g.initialized)
        mel_notif_init(NULL, NULL);
    if (opt.id.len == 0)
    {
        mel_log_error("notification", "channel_register with empty id");
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_INVALID_ARG;
    }
    Provider_Entry* prov = active_provider();
    if (prov == NULL)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_NO_PROVIDER;
    Mel_Notif_Status s = MEL_NOTIF_OK;
    if (prov->desc.channel_register != NULL)
    {
        s = prov->desc.channel_register(prov->desc.user, &opt);
        if (mel_notif_failed(s))
            return s;
    }
    if (!channel_registered(opt.id))
        mel_array_push(&g.channels, str_dup(opt.id));
    return s;
}

static Mel_Notif_Status caps_warn(const Mel_Notif_Content* c, Mel_Notif_Caps caps)
{
    Mel_Notif_Status warn = 0;
    if (c->action_count > 0 && (caps & MEL_NOTIF_CAP_ACTIONS) == 0)
        warn |= MEL_NOTIF_WARN_ACTIONS_DROPPED;
    if ((caps & MEL_NOTIF_CAP_REPLY) == 0)
        for (u32 i = 0; i < c->action_count; i++)
            if ((c->actions[i].flags & MEL_NOTIF_ACTION_TEXT_INPUT) != 0)
                warn |= MEL_NOTIF_WARN_REPLY_DROPPED;
    if ((c->icon.rgba != NULL || c->icon.path.len > 0) && (caps & MEL_NOTIF_CAP_ICON) == 0)
        warn |= MEL_NOTIF_WARN_IMAGE_DROPPED;
    if ((c->attachment.rgba != NULL || c->attachment.path.len > 0) && (caps & MEL_NOTIF_CAP_ATTACHMENT) == 0)
        warn |= MEL_NOTIF_WARN_IMAGE_DROPPED;
    if (c->progress.present && (caps & MEL_NOTIF_CAP_PROGRESS) == 0)
        warn |= MEL_NOTIF_WARN_PROGRESS_DROPPED;
    if (c->has_badge && (caps & MEL_NOTIF_CAP_BADGE) == 0)
        warn |= MEL_NOTIF_WARN_BADGE_DROPPED;
    if (c->sound_path.len > 0 && (caps & MEL_NOTIF_CAP_SOUND) == 0)
        warn |= MEL_NOTIF_WARN_SOUND_DROPPED;
    return warn;
}

static Mel_Notif_Result submit(const Mel_Notif_Content* content, Mel_Notif_Trigger trigger, bool scheduled)
{
    Mel_Notif_Result r = { .value = MEL_NOTIF_NULL, .status = MEL_NOTIF_OK };
    if (!g.initialized)
        mel_notif_init(NULL, NULL);
    if (content == NULL)
    {
        mel_log_error("notification", "post with NULL content");
        r.status = MEL_NOTIF_ERROR | MEL_NOTIF_ERR_INVALID_ARG;
        return r;
    }
    Provider_Entry* prov = active_provider();
    if (prov == NULL || prov->desc.post == NULL)
    {
        mel_log_error("notification", "post with no active provider");
        r.status = MEL_NOTIF_ERROR | MEL_NOTIF_ERR_NO_PROVIDER;
        return r;
    }
    Mel_Notif_Caps caps = provider_caps(prov);
    if (scheduled)
    {
        if (trigger.at_unix_ms == 0 && trigger.interval_ms == 0)
        {
            mel_log_error("notification", "schedule with empty trigger");
            r.status = MEL_NOTIF_ERROR | MEL_NOTIF_ERR_INVALID_ARG;
            return r;
        }
        if ((caps & MEL_NOTIF_CAP_SCHEDULE) == 0)
        {
            mel_log_error("notification", "provider '%s' cannot schedule", prov->desc.name ? prov->desc.name : "?");
            r.status = MEL_NOTIF_ERROR | MEL_NOTIF_ERR_UNSUPPORTED;
            return r;
        }
    }

    Mel_Notif_Status warn = caps_warn(content, caps);
    if (scheduled && (caps & MEL_NOTIF_CAP_SCHEDULE_PERSISTS) == 0)
        warn |= MEL_NOTIF_WARN_SCHEDULE_VOLATILE;
    if (scheduled && trigger.interval_ms > 0 && (caps & MEL_NOTIF_CAP_REPEAT) == 0)
        warn |= MEL_NOTIF_WARN_REPEAT_CLAMPED;

    Mel_Notif_Content lowered_content = *content;
    if ((caps & MEL_NOTIF_CAP_CHANNELS) != 0)
    {
        if (content->channel.len == 0)
        {
            if (!g.default_channel)
            {
                Mel_Notif_Channel_Opt def = { .id = S8("melody.default"), .label = S8("Notifications") };
                if (prov->desc.channel_register != NULL)
                    prov->desc.channel_register(prov->desc.user, &def);
                g.default_channel = true;
            }
            lowered_content.channel = S8("melody.default");
            warn |= MEL_NOTIF_WARN_DEFAULT_CHANNEL;
            mel_log_warn("notification", "post without channel on a channel platform; using 'melody.default'");
        }
        else if (!channel_registered(content->channel))
        {
            mel_log_error("notification", "post with unregistered channel '%.*s'", (int)content->channel.len, content->channel.data);
            r.status = MEL_NOTIF_ERROR | MEL_NOTIF_ERR_INVALID_ARG;
            return r;
        }
    }

    Notif_Slot ns = { 0 };
    ns.trigger = trigger;
    ns.scheduled = scheduled;
    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.notifs, &ns);
    Notif_Slot*        nsp = mel_notif__slot(h);
    Mel_Notif_Status   ds = slot_dup_content(nsp, &lowered_content);
    if (mel_notif_failed(ds))
    {
        notif_destroy_internal(h, false);
        r.status = ds;
        return r;
    }
    warn |= ds;

    Mel_Notif_Lowered lowered = {
        .token = mel_slotmap_handle_pack64(h),
        .content = &nsp->content,
        .trigger = trigger,
        .scheduled = scheduled,
    };
    Mel_Notif_Status ps = prov->desc.post(prov->desc.user, &lowered);
    if (mel_notif_failed(ps))
    {
        mel_log_error("notification", "provider '%s' post failed", prov->desc.name ? prov->desc.name : "?");
        notif_destroy_internal(h, false);
        r.status = ps;
        return r;
    }
    warn |= (ps & ~MEL_NOTIF_SEVERITY_MASK);

    r.value = (Mel_Notif){ h };
    r.status = warn ? (MEL_NOTIF_WARNED | warn) : MEL_NOTIF_OK;
    return r;
}

Mel_Notif_Result mel_notif_post(const Mel_Notif_Content* content)
{
    return submit(content, (Mel_Notif_Trigger){ 0 }, false);
}

Mel_Notif_Result mel_notif_schedule(const Mel_Notif_Content* content, Mel_Notif_Trigger trigger)
{
    return submit(content, trigger, true);
}

Mel_Notif_Status mel_notif_update(Mel_Notif n, const Mel_Notif_Content* content)
{
    Notif_Slot* ns = g.initialized ? mel_notif__slot(n.h) : NULL;
    if (ns == NULL)
    {
        mel_log_error("notification", "update on dead handle {index=%u, gen=%u}", n.h.index, n.h.generation);
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_DEAD_HANDLE;
    }
    if (content == NULL)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_INVALID_ARG;
    Provider_Entry* prov = active_provider();
    if (prov == NULL)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_NO_PROVIDER;

    Mel_Notif_Caps   caps = provider_caps(prov);
    Mel_Notif_Status warn = caps_warn(content, caps);

    slot_free_content(ns);
    Mel_Notif_Status ds = slot_dup_content(ns, content);
    if (mel_notif_failed(ds))
        return ds;
    warn |= ds;

    Mel_Notif_Lowered lowered = {
        .token = mel_slotmap_handle_pack64(n.h),
        .content = &ns->content,
        .trigger = ns->trigger,
        .scheduled = ns->scheduled,
    };
    Mel_Notif_Status s;
    if (prov->desc.update != NULL)
        s = prov->desc.update(prov->desc.user, &lowered);
    else if (prov->desc.post != NULL)
    {
        s = prov->desc.post(prov->desc.user, &lowered);
        warn |= MEL_NOTIF_WARN_UPDATE_REPOSTED;
    }
    else
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_UNSUPPORTED;
    if (mel_notif_failed(s))
        return s;
    warn |= (s & ~MEL_NOTIF_SEVERITY_MASK);
    return warn ? (MEL_NOTIF_WARNED | warn) : MEL_NOTIF_OK;
}

Mel_Notif_Status mel_notif_cancel(Mel_Notif n)
{
    if (!g.initialized || !mel_slotmap_alive(&g.notifs, n.h))
    {
        mel_log_error("notification", "cancel on dead handle {index=%u, gen=%u}", n.h.index, n.h.generation);
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_DEAD_HANDLE;
    }
    notif_destroy_internal(n.h, true);
    return MEL_NOTIF_OK;
}

void mel_notif_cancel_all(void)
{
    if (!g.initialized)
        return;
    Provider_Entry* prov = active_provider();
    if (prov != NULL && prov->desc.cancel_all != NULL)
        prov->desc.cancel_all(prov->desc.user);
    Mel_Array(Mel_SlotMap_Handle) live;
    mel_array_init(&live, g.alloc);
    for (u32 i = 0; i < g.notifs.slot_count; i++)
    {
        Mel_SlotMap_Handle h = mel_slotmap_handle_make(i, g.notifs.slots[i].generation);
        if (mel_slotmap_alive(&g.notifs, h))
            mel_array_push(&live, h);
    }
    for (usize i = 0; i < live.count; i++)
        notif_destroy_internal(live.items[i], false);
    mel_array_free(&live);
}

bool mel_notif_alive(Mel_Notif n) { return g.initialized && mel_slotmap_alive(&g.notifs, n.h); }
bool mel_notif_equal(Mel_Notif a, Mel_Notif b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

u32 mel_notif_poll_events(Mel_Notif_Event* out, u32 cap)
{
    if (!g.initialized || g.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.events, g.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Notif_Subscription mel_notif_subscribe(Mel_Executor* exec, Mel_Notif_Event_Callback cb, void* user)
{
    if (!g.initialized || g.events == NULL)
    {
        mel_log_error("notification", "subscribe before init; no channel");
        return MEL_NOTIF_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    if (target == NULL)
    {
        mel_log_error("notification", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_NOTIF_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Notif_Subscription){ sub.handle };
}

void mel_notif_unsubscribe(Mel_Notif_Subscription sub)
{
    if (!g.initialized || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}

static Mel_Notif token_to_notif(u64 token)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64(token);
    if (!mel_slotmap_alive(&g.notifs, h))
        return MEL_NOTIF_NULL;
    return (Mel_Notif){ h };
}

void mel_notif__dispatch_presented(u64 token)
{
    if (!g.initialized)
        return;
    fire_event((Mel_Notif_Event){ .kind = MEL_NOTIF_EVENT_PRESENTED, .notif = token_to_notif(token) });
}

void mel_notif__dispatch_activated(u64 token, str8 action_id, str8 reply, str8 payload)
{
    if (!g.initialized)
        return;
    Mel_Notif n = token_to_notif(token);
    Notif_Slot* ns = mel_notif__slot(n.h);
    str8 src[3] = { action_id, reply, (payload.len > 0 || ns == NULL) ? payload : ns->content.payload };
    str8 dst[3];
    if (!blob_views(src, dst, 3))
        return;
    Mel_Notif_Event_Kind kind = MEL_NOTIF_EVENT_ACTIVATED;
    if (dst[0].len > 0)
        kind |= MEL_NOTIF_EVENT_ACTION;
    if (reply.data != NULL)
        kind |= MEL_NOTIF_EVENT_REPLIED;
    fire_event((Mel_Notif_Event){ .kind = kind, .notif = n, .action_id = dst[0], .reply = dst[1], .payload = dst[2] });
}

void mel_notif__dispatch_dismissed(u64 token)
{
    if (!g.initialized)
        return;
    fire_event((Mel_Notif_Event){ .kind = MEL_NOTIF_EVENT_DISMISSED, .notif = token_to_notif(token) });
}

void mel_notif__dispatch_auth_changed(const mel_notif_auth* auth)
{
    if (!g.initialized || auth == NULL)
        return;
    if (g.auth == auth)
        return;
    g.auth = auth;
    fire_event((Mel_Notif_Event){ .kind = MEL_NOTIF_EVENT_AUTH_CHANGED, .notif = MEL_NOTIF_NULL, .auth = auth });
}

void mel_notif__dispatch_push_token(str8 token_bytes)
{
    if (!g.initialized)
        return;
    str8 src[1] = { token_bytes };
    str8 dst[1];
    if (!blob_views(src, dst, 1))
        return;
    fire_event((Mel_Notif_Event){ .kind = MEL_NOTIF_EVENT_PUSH_TOKEN, .notif = MEL_NOTIF_NULL, .payload = dst[0] });
}

void mel_notif__dispatch_push(str8 payload)
{
    if (!g.initialized)
        return;
    str8 src[1] = { payload };
    str8 dst[1];
    if (!blob_views(src, dst, 1))
        return;
    fire_event((Mel_Notif_Event){ .kind = MEL_NOTIF_EVENT_PUSH_RECEIVED, .notif = MEL_NOTIF_NULL, .payload = dst[0] });
}
