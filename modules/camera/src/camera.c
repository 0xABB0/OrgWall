#include <camera/camera.h>
#include <camera/provider.h>

#include "descriptors_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>
#include <collection.list/list.h>
#include <future/future.h>
#include <executor/executor.h>
#include <event/event.h>
#include <reactor/reactor.h>
#include <log/log.h>
#include <debug/assert.h>

#include <string.h>

#define MEL_CAMERA_HOTPLUG_RING_CAP 64u
#define MEL_CAMERA_FRAME_RING_CAP   1u

#define MEL_CAMERA_JOB_OP           1u
#define MEL_CAMERA_JOB_AUTH         2u

typedef struct
{
    Mel_Camera_Provider_Desc desc;
    u32                      generation;
    bool                     active;
} Provider_Entry;

typedef struct
{
    u32                      provider_idx;
    u64                      stable_id;
    str8                     name;
    const mel_camera_facing* facing;
    Mel_Camera_Modes         modes;

    Mel_Event*       frames;
    Mel_Future_Scope scope;
    bool             scope_init;
    bool             opened;
    bool             streaming;
    u64              frame_seq;
} Device_Slot;

typedef struct
{
    u64                stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Future       future;
    const Mel_Alloc* alloc;
    u32              kind;
} Job_Header;

typedef struct
{
    Job_Header             head;
    const mel_camera_auth* auth;
    bool                   resolved;
} Auth_Job;

typedef struct
{
    Job_Header        head;
    Mel_Camera_Status status;
    bool              resolved;
} Op_Job;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_Executor*    exec;

    Mel_SlotMap devices;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;

    Mel_Event* hotplug;
    u32        provider_gen;

    Auth_Job* pending_auth;
} Camera;

static Camera g;

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Device_Slot* device_slot(Mel_SlotMap_Handle h) { return (Device_Slot*)mel_slotmap_get(&g.devices, h); }

static Reg_Entry* reg_find(u32 prov, u64 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && g.registry.items[i].stable_id == stable_id)
            return &g.registry.items[i];
    return NULL;
}

static void hotplug_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("camera", "hotplug channel full (capacity %u); total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_hotplug(Mel_Camera_Event ev)
{
    if (g.hotplug != NULL)
        mel_event_fire(g.hotplug, &ev);
}

static void modes_copy(Mel_Camera_Modes* dst, const Mel_Camera_Mode* src, u32 n)
{
    mel_array_init(dst, g.alloc);
    for (u32 i = 0; i < n; i++)
        mel_array_push(dst, src[i]);
}

static void modes_copy_to(Mel_Camera_Modes* dst, const Device_Slot* s, const Mel_Alloc* a)
{
    mel_array_init(dst, a);
    for (usize i = 0; i < s->modes.count; i++)
        mel_array_push(dst, s->modes.items[i]);
}

Mel_Camera_Provider mel_camera_provider_register(const Mel_Camera_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Camera_Provider){ .index = idx, .generation = e.generation };
}

void mel_camera_provider_unregister(Mel_Camera_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_camera_init(const Mel_Alloc* alloc, Mel_Reactor* reactor)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.reactor = reactor;
    g.exec = reactor ? mel_reactor_executor(reactor) : mel_executor_inline();
    mel_slotmap_init(&g.devices, g.alloc, .item_size = sizeof(Device_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    g.provider_gen = 0;
    g.pending_auth = NULL;
    g.hotplug = mel_event_create(g.alloc, sizeof(Mel_Camera_Event), MEL_CAMERA_HOTPLUG_RING_CAP, mel_event_policy_latest(hotplug_overflow, NULL));
    g.initialized = true;
    mel_camera__register_host_providers();
    mel_camera_refresh();
}

static void device_teardown(Device_Slot* s)
{
    if (s->streaming || s->opened)
    {
        Provider_Entry* prov = provider_get(s->provider_idx);
        if (prov && s->streaming && prov->desc.stop)
            prov->desc.stop(prov->desc.user, s->stable_id);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, s->stable_id);
    }
    s->streaming = false;
    s->opened = false;
    if (s->scope_init)
    {
        mel_future_scope_teardown(&s->scope);
        s->scope_init = false;
    }
    if (s->frames != NULL)
    {
        mel_event_destroy(s->frames);
        s->frames = NULL;
    }
    mel_array_free(&s->modes);
}

void mel_camera_shutdown(void)
{
    if (!g.initialized)
        return;
    if (g.pending_auth && !g.pending_auth->resolved)
    {
        g.pending_auth->resolved = true;
        mel_future_cancel(&g.pending_auth->head.future);
    }
    g.pending_auth = NULL;

    for (usize i = 0; i < g.registry.count; i++)
    {
        Device_Slot* s = device_slot(g.registry.items[i].handle);
        if (s)
            device_teardown(s);
    }
    mel_event_destroy(g.hotplug);
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.devices);
    memset(&g, 0, sizeof g);
}

static void device_update(Device_Slot* s, const Mel_Camera_Raw* raw)
{
    s->name = raw->name;
    s->facing = raw->facing ? raw->facing : &mel_camera_unknown;
    mel_array_clear(&s->modes);
    for (u32 i = 0; i < raw->mode_count; i++)
        mel_array_push(&s->modes, raw->modes[i]);
}

u32 mel_camera_refresh(void)
{
    if (!g.initialized)
        return 0;

    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.registry.count; i++)
        mel_array_push(&seen, false);

    Mel_Array(Mel_Camera_Raw) tmp;
    mel_array_init(&tmp, g.alloc);
    mel_array_reserve(&tmp, 8);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        mel_array_clear(&tmp);
        u32 n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        while (n > tmp.capacity)
        {
            mel_array_reserve(&tmp, n);
            n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        }
        for (u32 i = 0; i < n; i++)
        {
            Mel_Camera_Raw* raw = &tmp.items[i];
            Reg_Entry*      e = reg_find(pi, raw->stable_id);
            if (e)
            {
                seen.items[(usize)(e - g.registry.items)] = true;
                Device_Slot* s = device_slot(e->handle);
                if (s)
                {
                    device_update(s, raw);
                    fire_hotplug((Mel_Camera_Event){ .camera = { e->handle }, .facing = s->facing, .changed = true });
                }
                continue;
            }
            Device_Slot slot;
            memset(&slot, 0, sizeof slot);
            slot.provider_idx = pi;
            slot.stable_id = raw->stable_id;
            slot.name = raw->name;
            slot.facing = raw->facing ? raw->facing : &mel_camera_unknown;
            modes_copy(&slot.modes, raw->modes, raw->mode_count);
            Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
            Reg_Entry          re = { .stable_id = raw->stable_id, .provider_idx = pi, .handle = h };
            mel_array_push(&g.registry, re);
            mel_array_push(&seen, true);
            mel_log_info("camera", "device added: stable_id=%llu facing=%s", (unsigned long long)raw->stable_id, mel_camera_facing_name(slot.facing));
            fire_hotplug((Mel_Camera_Event){ .camera = { h }, .facing = slot.facing, .added = true });
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
        Mel_SlotMap_Handle       h = g.registry.items[i].handle;
        Device_Slot*             s = device_slot(h);
        const mel_camera_facing* facing = s ? s->facing : &mel_camera_unknown;
        if (s)
            device_teardown(s);
        mel_slotmap_remove(&g.devices, h);
        fire_hotplug((Mel_Camera_Event){ .camera = { h }, .facing = facing, .removed = true });

        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    mel_array_free(&seen);
    return (u32)g.registry.count;
}

u32 mel_camera_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_camera_list(Mel_Camera* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Camera){ g.registry.items[i].handle };
    return n;
}

Mel_Camera_Describe_Result mel_camera_describe(Mel_Camera c, const Mel_Alloc* a)
{
    Mel_Camera_Describe_Result r = { 0 };
    Device_Slot*               s = g.initialized ? device_slot(c.h) : NULL;
    if (!s)
    {
        mel_log_error("camera", "describe on dead handle {index=%u, gen=%u}", c.h.index, c.h.generation);
        r.status = MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
        return r;
    }
    if (!a)
    {
        mel_log_error("camera", "describe requires an allocator; got NULL");
        r.status = MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED;
        return r;
    }
    r.value.name = s->name;
    r.value.facing = s->facing;
    r.value.alloc = a;
    modes_copy_to(&r.value.modes, s, a);
    r.status = MEL_CAMERA_OK;
    return r;
}

void mel_camera_describe_free(Mel_Camera_Describe_Result* r)
{
    if (r)
        mel_array_free(&r->value.modes);
}

bool mel_camera_alive(Mel_Camera c) { return g.initialized && mel_slotmap_alive(&g.devices, c.h); }

bool mel_camera_equal(Mel_Camera a, Mel_Camera b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

Mel_Camera_Hotplug_Sub mel_camera_subscribe(Mel_Executor* exec, Mel_Camera_Event_Callback cb, void* user)
{
    if (!g.initialized || g.hotplug == NULL)
    {
        mel_log_error("camera", "subscribe before init; no channel");
        return MEL_CAMERA_HOTPLUG_SUB_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    Mel_Event_Sub sub = mel_event_subscribe_push(g.hotplug, target, (Mel_Event_Callback)cb, user);
    return (Mel_Camera_Hotplug_Sub){ sub.handle };
}

void mel_camera_unsubscribe(Mel_Camera_Hotplug_Sub sub)
{
    if (!g.initialized || g.hotplug == NULL)
        return;
    mel_event_unsubscribe(g.hotplug, (Mel_Event_Sub){ sub.handle });
}

static const mel_camera_auth* first_provider_authorization(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* pe = &g.providers.items[i];
        if (pe->active && pe->desc.authorization)
            return pe->desc.authorization(pe->desc.user);
    }
    return &mel_camera_auth_not_determined;
}

const mel_camera_auth* mel_camera_authorization(void)
{
    if (!g.initialized)
        return &mel_camera_auth_not_determined;
    return first_provider_authorization();
}

static void auth_resolve(Auth_Job* j, const mel_camera_auth* auth)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->auth = auth;
    Mel_Future_Status fs = mel_camera_auth_is_granted(auth) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR;
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_future_resolve(&j->head.future, (void*)auth, fs);
}

static void core_on_auth(void* token, const mel_camera_auth* auth)
{
    MEL_UNUSED(token);
    auth_resolve(g.pending_auth, auth);
}

Mel_Future* mel_camera_authorize(const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    const Mel_Alloc* alloc = a ? a : g.alloc;
    Auth_Job*        j = mel_alloc_type(alloc, Auth_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->head.alloc = alloc;
    j->head.kind = MEL_CAMERA_JOB_AUTH;
    mel_future_init(&j->head.future, NULL, alloc);
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
        return &j->head.future;
    }
    Mel_Camera_Sink sink = { .on_auth = core_on_auth, .token = NULL };
    prov->desc.authorize(prov->desc.user, sink);
    return &j->head.future;
}

const mel_camera_auth* mel_camera_future_auth(const Mel_Future* f)
{
    const mel_camera_auth* a = f ? (const mel_camera_auth*)mel_future_value((Mel_Future*)f) : NULL;
    return a ? a : &mel_camera_auth_not_determined;
}

static void frame_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(info);
    MEL_UNUSED(user);
}

static Mel_Event* device_frames(Device_Slot* s)
{
    if (s->frames == NULL)
        s->frames = mel_event_create(g.alloc, sizeof(Mel_Camera_Frame), MEL_CAMERA_FRAME_RING_CAP, mel_event_policy_latest(frame_overflow, NULL));
    return s->frames;
}

static void core_on_frame(void* token, const Mel_Camera_Frame* frame)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Device_Slot*       s = device_slot(h);
    if (!s || s->frames == NULL)
        return;
    Mel_Camera_Frame f = *frame;
    f.sequence = s->frame_seq++;
    mel_event_fire(s->frames, &f);
}

static void core_on_event(void* token, Mel_Camera_Event ev)
{
    MEL_UNUSED(token);
    fire_hotplug(ev);
}

static Mel_Camera_Sink device_sink(Mel_SlotMap_Handle h)
{
    return (Mel_Camera_Sink){
        .on_frame = core_on_frame,
        .on_event = core_on_event,
        .on_auth = core_on_auth,
        .token = (void*)(usize)mel_slotmap_handle_pack64(h),
    };
}

static Op_Job* op_job_new(const Mel_Alloc* a)
{
    const Mel_Alloc* alloc = a ? a : g.alloc;
    Op_Job*          j = mel_alloc_type(alloc, Op_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->head.alloc = alloc;
    j->head.kind = MEL_CAMERA_JOB_OP;
    mel_future_init(&j->head.future, NULL, alloc);
    return j;
}

static void op_resolve(Op_Job* j, Mel_Camera_Status status)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->status = status;
    mel_future_resolve(&j->head.future, NULL, (Mel_Future_Status)(status & MEL_CAMERA_SEVERITY_MASK));
}

static Mel_Future* op_fail(Op_Job* j, Mel_Camera_Status status)
{
    op_resolve(j, status);
    return &j->head.future;
}

Mel_Future* mel_camera_open(Mel_Camera c, Mel_Camera_Config cfg, const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    Op_Job* j = op_job_new(a);
    if (!j)
        return NULL;
    Device_Slot* s = device_slot(c.h);
    if (!s)
    {
        mel_log_error("camera", "open on dead handle");
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE);
    }
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.open)
    {
        mel_log_error("camera", "device has no open provider");
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED);
    }
    if (s->opened)
    {
        mel_log_error("camera", "open on already-open device");
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_BUSY);
    }
    if (!cfg.format || cfg.width <= 0 || cfg.height <= 0)
    {
        mel_log_error("camera", "open with invalid config");
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED);
    }

    device_frames(s);
    if (!s->scope_init)
    {
        mel_future_scope_init(&s->scope, g.alloc);
        s->scope_init = true;
    }

    Mel_Camera_Sink sink = device_sink(c.h);
    if (!prov->desc.open(prov->desc.user, s->stable_id, cfg, sink))
    {
        mel_log_error("camera", "provider open failed");
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED);
    }
    s->opened = true;
    mel_future_scope_adopt(&s->scope, &j->head.future);
    op_resolve(j, MEL_CAMERA_OK);
    return &j->head.future;
}

Mel_Future* mel_camera_start(Mel_Camera c, const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    Op_Job* j = op_job_new(a);
    if (!j)
        return NULL;
    Device_Slot* s = device_slot(c.h);
    if (!s)
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE);
    if (!s->opened)
    {
        mel_log_error("camera", "start before open");
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_BUSY);
    }
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.start)
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED);

    Mel_Camera_Status st = prov->desc.start(prov->desc.user, s->stable_id);
    if (mel_camera_status_failed(st))
        return op_fail(j, st);
    s->streaming = true;
    if (s->scope_init)
        mel_future_scope_adopt(&s->scope, &j->head.future);
    op_resolve(j, st);
    return &j->head.future;
}

Mel_Future* mel_camera_stop(Mel_Camera c, const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    Op_Job* j = op_job_new(a);
    if (!j)
        return NULL;
    Device_Slot* s = device_slot(c.h);
    if (!s)
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE);
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!s->streaming)
        return op_fail(j, MEL_CAMERA_OK);
    if (!prov || !prov->desc.stop)
        return op_fail(j, MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED);

    Mel_Camera_Status st = prov->desc.stop(prov->desc.user, s->stable_id);
    s->streaming = false;
    op_resolve(j, st);
    return &j->head.future;
}

void mel_camera_close(Mel_Camera c)
{
    if (!g.initialized)
        return;
    Device_Slot* s = device_slot(c.h);
    if (!s)
        return;
    device_teardown(s);
}

Mel_Camera_Status mel_camera_future_status(const Mel_Future* f)
{
    if (!f)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_CANCELLED;
    Mel_Future_Status s = mel_future_status((Mel_Future*)f);
    if (s & MEL_FUTURE_CANCELLED)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_CANCELLED;
    const Job_Header* h = mel_container_of(f, Job_Header, future);
    if (h->kind == MEL_CAMERA_JOB_AUTH)
    {
        const Auth_Job* aj = mel_container_of(h, Auth_Job, head);
        return mel_camera_auth_is_granted(aj->auth) ? MEL_CAMERA_OK : (MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_DENIED);
    }
    return mel_container_of(h, Op_Job, head)->status;
}

void mel_camera_future_free(Mel_Future* f)
{
    if (!f)
        return;
    Job_Header* h = mel_container_of(f, Job_Header, future);
    if (h->kind == MEL_CAMERA_JOB_AUTH)
    {
        Auth_Job* aj = mel_container_of(h, Auth_Job, head);
        if (g.pending_auth == aj)
            g.pending_auth = NULL;
    }
    else if (h->kind != MEL_CAMERA_JOB_OP)
    {
        mel_log_error("camera", "future_free on a future not produced by this module (kind=%u)", h->kind);
        return;
    }
    mel_dealloc(h->alloc, h);
}

Mel_Event* mel_camera_frames(Mel_Camera c)
{
    if (!g.initialized)
        return NULL;
    Device_Slot* s = device_slot(c.h);
    if (!s)
    {
        mel_log_error("camera", "frames on dead handle");
        return NULL;
    }
    return device_frames(s);
}

Mel_Camera_Frame_Sub mel_camera_frame_subscribe(Mel_Camera c, Mel_Camera_Frame_Callback cb, void* user)
{
    Mel_Event* ev = mel_camera_frames(c);
    if (!ev || !cb)
        return MEL_CAMERA_FRAME_SUB_NULL;
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, mel_executor_inline(), (Mel_Event_Callback)cb, user);
    return (Mel_Camera_Frame_Sub){ sub.handle };
}

void mel_camera_frame_unsubscribe(Mel_Camera c, Mel_Camera_Frame_Sub sub)
{
    if (!g.initialized)
        return;
    Device_Slot* s = device_slot(c.h);
    if (!s || s->frames == NULL)
        return;
    mel_event_unsubscribe(s->frames, (Mel_Event_Sub){ sub.handle });
}

bool mel_camera_frame_pull(Mel_Camera c, Mel_Camera_Frame* out)
{
    MEL_UNUSED(c);
    MEL_UNUSED(out);
    mel_log_error("camera", "pull is forbidden on frames: the image borrows the OS buffer and is valid only during the push callback (frame lifetime); use mel_camera_frame_subscribe");
    return false;
}

void* mel_camera_native(Mel_Camera c)
{
    if (!g.initialized)
        return NULL;
    Device_Slot* s = device_slot(c.h);
    if (!s)
        return NULL;
    Provider_Entry* prov = provider_get(s->provider_idx);
    return (prov && prov->desc.native) ? prov->desc.native(prov->desc.user, s->stable_id) : NULL;
}
