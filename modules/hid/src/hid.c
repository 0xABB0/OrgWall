#include <hid/hid.h>
#include <hid/events.h>
#include <hid/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <collection/list.h>
#include <event/event.h>
#include <executor/executor.h>
#include <future/future.h>
#include <port/port.h>
#include <vat/vat.h>
#include <log/log.h>

#include <string.h>

#include "events_internal.h"
#include "hid_internal.h"

#define MEL_HID_EVENTS_CAP 256

typedef struct
{
    Mel_Hid_Provider_Desc desc;
    u32                   generation;
    bool                  active;
} Provider_Entry;

typedef struct
{
    u32                provider_idx;
    u64                stable_id;
    Mel_Hid_Descriptor desc;
    bool               open;
    Mel_Hid_Channel    channel;
} Device_Slot;

typedef struct
{
    u64                stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Hid_Raw raw;
    u32         prov;
} Gathered;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;
    Mel_SlotMap      devices;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    u32 provider_gen;
    u64 change_count;

    Mel_Event*    events;
    Mel_Event_Sub poll_sub;
} Hid;

static Hid g;

static bool g_skip_host_providers;

void mel_hid__set_skip_host_providers(bool skip) { g_skip_host_providers = skip; }

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

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("hid", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_event(Mel_Hid_Event ev)
{
    if (g.events != NULL)
        mel_event_fire(g.events, &ev);
}

Mel_Hid_Provider mel_hid_provider_register(const Mel_Hid_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Hid_Provider){ .index = idx, .generation = e.generation };
}

void mel_hid_provider_unregister(Mel_Hid_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_hid_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.exec = exec;
    mel_slotmap_init(&g.devices, g.alloc, .item_size = sizeof(Device_Slot), .initial_capacity = 8);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    g.provider_gen = 0;
    g.change_count = 0;

    g.events = mel_event_create(g.alloc, sizeof(Mel_Hid_Event), MEL_HID_EVENTS_CAP, mel_event_policy_latest(events_overflow_report, NULL));
    g.poll_sub = g.events != NULL ? mel_event_subscribe_pull(g.events, NULL) : MEL_EVENT_SUB_NULL;

    g.initialized = true;
    if (!g_skip_host_providers)
        mel_hid__register_host_providers(g.alloc);
    mel_hid_refresh();
}

void mel_hid_init(const Mel_Alloc* alloc) { mel_hid_init_ex(alloc, NULL); }

static void close_slot(Device_Slot* s)
{
    if (s->open)
    {
        Provider_Entry* prov = provider_get(s->provider_idx);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, s->stable_id, s->channel);
        s->open = false;
        s->channel = (Mel_Hid_Channel){ .fd = MEL_HID_NO_FD };
    }
}

void mel_hid_shutdown(void)
{
    if (!g.initialized)
        return;
    for (usize i = 0; i < g.registry.count; i++)
    {
        Device_Slot* s = device_slot(g.registry.items[i].handle);
        if (s)
            close_slot(s);
    }
    if (g.events != NULL)
        mel_event_unsubscribe(g.events, g.poll_sub);
    mel_event_destroy(g.events);
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.devices);
    memset(&g, 0, sizeof g);
}

u32 mel_hid_refresh(void)
{
    if (!g.initialized)
        mel_hid_init(NULL);

    Mel_Array(Gathered) gs;
    mel_array_init(&gs, g.alloc);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        Mel_Array(Mel_Hid_Raw) tmp;
        mel_array_init(&tmp, g.alloc);
        mel_array_reserve(&tmp, 16);
        u32 n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        while (n > tmp.capacity)
        {
            mel_array_reserve(&tmp, n);
            n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        }
        for (u32 i = 0; i < n; i++)
        {
            Gathered gthr = { .raw = tmp.items[i], .prov = pi };
            mel_array_push(&gs, gthr);
        }
        mel_array_free(&tmp);
    }

    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.registry.count; i++)
        mel_array_push(&seen, false);

    bool topology_moved = false;

    for (usize i = 0; i < gs.count; i++)
    {
        Gathered*  gt = &gs.items[i];
        Reg_Entry* e = reg_find(gt->prov, gt->raw.stable_id);
        if (e)
        {
            seen.items[(usize)(e - g.registry.items)] = true;
            Device_Slot* s = device_slot(e->handle);
            if (!s)
                continue;
            u32 fields = mel_hid_events__changed_fields(&s->desc, &gt->raw.desc);
            if (fields == 0)
                continue;
            s->desc = gt->raw.desc;
            fire_event((Mel_Hid_Event){ .kind = MEL_HID_EVENT_CHANGED, .device = { e->handle }, .changed_fields = fields });
            continue;
        }

        Device_Slot        slot = { .provider_idx = gt->prov, .stable_id = gt->raw.stable_id, .desc = gt->raw.desc, .open = false, .channel = { .fd = MEL_HID_NO_FD } };
        Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
        Reg_Entry          re = { .stable_id = gt->raw.stable_id, .provider_idx = gt->prov, .handle = h };
        mel_array_push(&g.registry, re);
        mel_array_push(&seen, true);
        topology_moved = true;
        fire_event((Mel_Hid_Event){ .kind = MEL_HID_EVENT_ADDED, .device = { h } });
        mel_log_info("hid", "device added: vid=%04x pid=%04x stable_id=%llu", gt->raw.desc.vendor_id, gt->raw.desc.product_id, (unsigned long long)gt->raw.stable_id);
    }

    for (usize i = 0; i < g.registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.registry.items[i].handle;
        Device_Slot*       s = device_slot(h);
        if (s)
            close_slot(s);
        mel_slotmap_remove(&g.devices, h);
        topology_moved = true;
        fire_event((Mel_Hid_Event){ .kind = MEL_HID_EVENT_REMOVED, .device = { h } });

        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    if (topology_moved)
        g.change_count++;

    mel_array_free(&seen);
    mel_array_free(&gs);
    return (u32)g.registry.count;
}

u32 mel_hid_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_hid_list(Mel_Hid_Device* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Hid_Device){ g.registry.items[i].handle };
    return n;
}

u64 mel_hid_device_change_count(void) { return g.initialized ? g.change_count : 0; }

Mel_Hid_Describe_Result mel_hid_describe(Mel_Hid_Device d)
{
    Mel_Hid_Describe_Result r = { 0 };
    Device_Slot*            s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("hid", "describe on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_HID_ERROR | MEL_HID_INVALID_HANDLE;
        return r;
    }
    r.value = s->desc;
    r.status = MEL_HID_OK;
    return r;
}

bool mel_hid_alive(Mel_Hid_Device d) { return g.initialized && mel_slotmap_alive(&g.devices, d.h); }

bool mel_hid_equal(Mel_Hid_Device a, Mel_Hid_Device b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

Mel_Hid_Status mel_hid_open(Mel_Hid_Device d)
{
    if (!g.initialized)
        return MEL_HID_ERROR | MEL_HID_NO_BACKEND;
    Device_Slot* s = device_slot(d.h);
    if (!s)
    {
        mel_log_error("hid", "open on dead handle");
        return MEL_HID_ERROR | MEL_HID_INVALID_HANDLE;
    }
    if (s->open)
        return MEL_HID_OK;
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.open)
        return MEL_HID_ERROR | MEL_HID_UNSUPPORTED;
    Mel_Hid_Channel ch = { .fd = MEL_HID_NO_FD };
    Mel_Hid_Status  st = prov->desc.open(prov->desc.user, s->stable_id, &ch);
    if (mel_hid_failed(st))
        return st;
    s->open = true;
    s->channel = ch;
    return st;
}

void mel_hid_close(Mel_Hid_Device d)
{
    if (!g.initialized)
        return;
    Device_Slot* s = device_slot(d.h);
    if (s)
        close_slot(s);
}

bool mel_hid_is_open(Mel_Hid_Device d)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    return s && s->open;
}

typedef Mel_Hid_Io_Result (*Io_Thunk)(Provider_Entry* prov, Device_Slot* s, void* ctx);

static Mel_Hid_Io_Result io_dispatch(Mel_Hid_Device d, Io_Thunk thunk, void* ctx)
{
    Mel_Hid_Io_Result r = { .bytes = 0, .status = MEL_HID_ERROR };
    if (!g.initialized)
    {
        r.status |= MEL_HID_NO_BACKEND;
        return r;
    }
    Device_Slot* s = device_slot(d.h);
    if (!s)
    {
        mel_log_error("hid", "io on dead handle");
        r.status |= MEL_HID_INVALID_HANDLE;
        return r;
    }
    if (!s->open)
    {
        mel_log_error("hid", "io on closed device");
        r.status |= MEL_HID_NOT_OPEN;
        return r;
    }
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov)
    {
        r.status |= MEL_HID_DEVICE_LOST;
        return r;
    }
    return thunk(prov, s, ctx);
}

typedef struct
{
    const u8* data;
    usize     len;
} Write_Ctx;

static Mel_Hid_Io_Result write_thunk(Provider_Entry* prov, Device_Slot* s, void* ctx)
{
    Write_Ctx* w = ctx;
    if (!prov->desc.write)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    return prov->desc.write(prov->desc.user, s->channel, w->data, w->len);
}

Mel_Hid_Io_Result mel_hid_write(Mel_Hid_Device d, const u8* data, usize len)
{
    Write_Ctx ctx = { .data = data, .len = len };
    return io_dispatch(d, write_thunk, &ctx);
}

typedef struct
{
    u8*   out;
    usize cap;
    i32   timeout_ms;
} Read_Ctx;

static Mel_Hid_Io_Result read_thunk(Provider_Entry* prov, Device_Slot* s, void* ctx)
{
    Read_Ctx* rc = ctx;
    if (!prov->desc.read)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    return prov->desc.read(prov->desc.user, s->channel, rc->out, rc->cap, rc->timeout_ms);
}

Mel_Hid_Io_Result mel_hid_read(Mel_Hid_Device d, u8* out, usize cap, i32 timeout_ms)
{
    Read_Ctx ctx = { .out = out, .cap = cap, .timeout_ms = timeout_ms };
    return io_dispatch(d, read_thunk, &ctx);
}

Mel_Hid_Io_Result mel_hid__read_now(Mel_Hid_Device d, u8* out, usize cap, i32 timeout_ms) { return mel_hid_read(d, out, cap, timeout_ms); }

typedef struct
{
    u8    report_id;
    u8*   out;
    usize cap;
} Get_Feature_Ctx;

static Mel_Hid_Io_Result get_feature_thunk(Provider_Entry* prov, Device_Slot* s, void* ctx)
{
    Get_Feature_Ctx* gf = ctx;
    if (!prov->desc.get_feature)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    return prov->desc.get_feature(prov->desc.user, s->channel, gf->report_id, gf->out, gf->cap);
}

Mel_Hid_Io_Result mel_hid_get_feature(Mel_Hid_Device d, u8 report_id, u8* out, usize cap)
{
    Get_Feature_Ctx ctx = { .report_id = report_id, .out = out, .cap = cap };
    return io_dispatch(d, get_feature_thunk, &ctx);
}

static Mel_Hid_Io_Result send_feature_thunk(Provider_Entry* prov, Device_Slot* s, void* ctx)
{
    Write_Ctx* w = ctx;
    if (!prov->desc.send_feature)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    return prov->desc.send_feature(prov->desc.user, s->channel, w->data, w->len);
}

Mel_Hid_Io_Result mel_hid_send_feature(Mel_Hid_Device d, const u8* data, usize len)
{
    Write_Ctx ctx = { .data = data, .len = len };
    return io_dispatch(d, send_feature_thunk, &ctx);
}

typedef struct
{
    u8*   out;
    usize cap;
} Buf_Ctx;

static Mel_Hid_Io_Result get_report_descriptor_thunk(Provider_Entry* prov, Device_Slot* s, void* ctx)
{
    Buf_Ctx* b = ctx;
    if (!prov->desc.get_report_descriptor)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    return prov->desc.get_report_descriptor(prov->desc.user, s->channel, b->out, b->cap);
}

Mel_Hid_Io_Result mel_hid_get_report_descriptor(Mel_Hid_Device d, u8* out, usize cap)
{
    Buf_Ctx ctx = { .out = out, .cap = cap };
    return io_dispatch(d, get_report_descriptor_thunk, &ctx);
}

typedef struct
{
    u8    string_index;
    u8*   out;
    usize cap;
} Get_String_Ctx;

static Mel_Hid_Io_Result get_string_thunk(Provider_Entry* prov, Device_Slot* s, void* ctx)
{
    Get_String_Ctx* gs = ctx;
    if (!prov->desc.get_string)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    return prov->desc.get_string(prov->desc.user, s->channel, gs->string_index, gs->out, gs->cap);
}

Mel_Hid_Io_Result mel_hid_get_string(Mel_Hid_Device d, u8 string_index, u8* out, usize cap)
{
    Get_String_Ctx ctx = { .string_index = string_index, .out = out, .cap = cap };
    return io_dispatch(d, get_string_thunk, &ctx);
}

bool mel_hid__channel(Mel_Hid_Device d, Mel_Hid_Channel* out_channel, u32* out_provider_idx, u64* out_stable_id)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s || !s->open)
        return false;
    if (out_channel)
        *out_channel = s->channel;
    if (out_provider_idx)
        *out_provider_idx = s->provider_idx;
    if (out_stable_id)
        *out_stable_id = s->stable_id;
    return true;
}

void* mel_hid_native(Mel_Hid_Device d)
{
    if (!g.initialized)
        return NULL;
    Device_Slot* s = device_slot(d.h);
    if (!s || !s->open)
        return NULL;
    Provider_Entry* prov = provider_get(s->provider_idx);
    return (prov && prov->desc.native) ? prov->desc.native(prov->desc.user, s->channel) : NULL;
}

typedef struct
{
    Mel_Future        future;
    Mel_Hid_Io_Result result;
    const Mel_Alloc*  alloc;

    Mel_Hid_Device d;
    u8*            buffer;
    usize          len;

    Mel_Port*       port;
    Mel_Future*     inner;
    Mel_Port_Op     op;
    Mel_Task        translate;
    Mel_Vat_Source* source;
    bool            released;
} Async_Read;

static void async_resolve(Async_Read* ar, Mel_Hid_Io_Result res)
{
    ar->result = res;
    Mel_Future_Status fs = res.status & MEL_FUTURE_SEVERITY_MASK;
    if (res.status & MEL_HID_PARTIAL)
        fs |= MEL_FUTURE_PARTIAL;
    if (res.status & MEL_HID_CANCELLED)
        fs |= MEL_FUTURE_CANCELLED;
    mel_future_resolve(&ar->future, &ar->result, fs);
}

static void translate_port_result(Mel_Task* t)
{
    Async_Read*            ar = mel_container_of(t, Async_Read, translate);
    const Mel_Port_Result* pr = mel_port_future_result(ar->inner);
    Mel_Hid_Io_Result      res = { .bytes = 0, .status = MEL_HID_OK };
    if (pr)
    {
        res.bytes = pr->bytes_transferred;
        if ((pr->status & MEL_PORT_SEVERITY_MASK) == MEL_PORT_ERROR)
            res.status = MEL_HID_ERROR;
        else if ((pr->status & MEL_PORT_SEVERITY_MASK) == MEL_PORT_WARNED)
            res.status = MEL_HID_WARNED;
        if (pr->status & MEL_PORT_CANCELLED)
            res.status |= MEL_HID_CANCELLED;
        if (pr->status & MEL_PORT_EOF)
            res.status |= MEL_HID_DEVICE_LOST;
        if (pr->status & MEL_PORT_PEER_CLOSE)
            res.status |= MEL_HID_DEVICE_LOST;
        if (pr->status & MEL_PORT_PARTIAL)
            res.status |= MEL_HID_PARTIAL;
    }
    else
    {
        res.status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    }
    mel_port_future_release(ar->inner);
    ar->inner = NULL;
    if (ar->released)
    {
        mel_dealloc(ar->alloc, ar);
        return;
    }
    async_resolve(ar, res);
}

static i64 async_read_deadline(Mel_Vat_Source* source)
{
    (void)source;
    return 0;
}

static bool async_read_drain(Mel_Vat_Source* source, u32 budget)
{
    (void)budget;
    Async_Read*       ar = mel_vat_source_state(source);
    Mel_Hid_Io_Result res = mel_hid__read_now(ar->d, ar->buffer, ar->len, MEL_HID_TIMEOUT_POLL);
    if (mel_hid_would_block(res.status))
        return false;
    ar->source = NULL;
    mel_vat_source_close(source);
    async_resolve(ar, res);
    return false;
}

static const Mel_Vat_Source_Vtbl ASYNC_READ_VT = {
    .wakeables = NULL,
    .deadline = async_read_deadline,
    .drain = async_read_drain,
    .cancel = NULL,
};

Mel_Future* mel_hid_read_async_opt(Mel_Hid_Device d, Mel_Hid_Read_Async_Opt opt)
{
    if (!g.initialized)
        return NULL;
    Mel_Hid_Channel ch;
    if (!mel_hid__channel(d, &ch, NULL, NULL))
    {
        mel_log_error("hid", "read_async on closed or dead handle");
        return NULL;
    }

    Mel_Executor* deliver = opt.deliver ? opt.deliver : g.exec;

    Async_Read* ar = mel_alloc_type(g.alloc, Async_Read);
    if (!ar)
        return NULL;
    memset(ar, 0, sizeof *ar);
    mel_future_init(&ar->future, NULL, g.alloc);
    ar->future.value = &ar->result;
    ar->alloc = g.alloc;
    ar->d = d;
    ar->buffer = opt.buffer;
    ar->len = opt.len;
    ar->port = opt.port;

    if (opt.port && mel_port_available(opt.port) && ch.fd != MEL_HID_NO_FD)
    {
        ar->inner = mel_port_read(opt.port, .fd = ch.fd, .buffer = opt.buffer, .len = opt.len, .deliver = deliver, .out_op = &ar->op);
        if (!ar->inner)
        {
            mel_dealloc(g.alloc, ar);
            return NULL;
        }
        mel_task_init(&ar->translate, translate_port_result);
        mel_future_then(ar->inner, &ar->translate, deliver);
        return &ar->future;
    }

    Mel_Vat* vat = opt.port ? mel_port_vat(opt.port) : NULL;
    if (!vat)
    {
        mel_log_warn("hid", "read_async without a pollable fd and without a vat; no async substrate");
        mel_dealloc(g.alloc, ar);
        return NULL;
    }
    ar->source = mel_vat_source_open(vat, &ASYNC_READ_VT, ar);
    if (!ar->source)
    {
        mel_dealloc(g.alloc, ar);
        return NULL;
    }
    return &ar->future;
}

const Mel_Hid_Io_Result* mel_hid_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Async_Read* ar = mel_container_of(f, Async_Read, future);
    return &ar->result;
}

void mel_hid_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Async_Read* ar = mel_container_of(f, Async_Read, future);
    if (ar->source)
    {
        mel_vat_source_close(ar->source);
        ar->source = NULL;
    }
    if (ar->inner)
    {
        ar->released = true;
        mel_port_cancel(ar->port, ar->op);
        return;
    }
    mel_dealloc(ar->alloc, ar);
}

u32 mel_hid_poll_events(Mel_Hid_Event* out, u32 cap)
{
    if (!g.initialized || g.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.events, g.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Hid_Subscription mel_hid_subscribe(Mel_Executor* exec, Mel_Hid_Event_Callback cb, void* user)
{
    if (!g.initialized || g.events == NULL)
    {
        mel_log_error("hid", "subscribe before init; no channel");
        return MEL_HID_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    if (target == NULL)
    {
        mel_log_error("hid", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_HID_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Hid_Subscription){ sub.handle };
}

void mel_hid_unsubscribe(Mel_Hid_Subscription sub)
{
    if (!g.initialized || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}
