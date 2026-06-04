#include <input/input.h>
#include <input/events.h>
#include <input/keyboard.h>
#include <input/mouse.h>
#include <input/touch.h>
#include <input/pen.h>
#include <input/provider.h>

#include "input_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>
#include <event/event.h>
#include <log/log.h>

#include <string.h>

#define MEL_INPUT_DEVICE_EVENTS_CAP 128
#define MEL_INPUT_STREAM_EVENTS_CAP 512

typedef struct
{
    Mel_Input_Provider_Desc desc;
    u32                     generation;
    bool                    active;
} Provider_Entry;

typedef struct
{
    u32                         provider_idx;
    u64                         stable_id;
    Mel_Input_Device_Descriptor desc;
} Device_Slot;

typedef struct
{
    u64                provider_idx;
    u64                stable_id;
    Mel_SlotMap_Handle handle;
    bool               seen;
} Reg_Entry;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;

    Mel_SlotMap devices;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    u32 provider_gen;

    Mel_Event*    device_events;
    Mel_Event_Sub device_poll_sub;

    Mel_Event*    stream_events;
    Mel_Event_Sub stream_poll_sub;
} Input;

static Input g;

struct Mel_Input_Sink
{
    int dummy;
};

static Mel_Input_Sink g_sink;

Mel_Input_Sink* mel_input__sink(void) { return &g_sink; }

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Reg_Entry* reg_find_stable(u32 prov, u64 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && g.registry.items[i].stable_id == stable_id)
            return &g.registry.items[i];
    return NULL;
}

static Reg_Entry* reg_find_by_handle(Mel_Input_Device d, u32* out_prov)
{
    Device_Slot* s = (Device_Slot*)mel_slotmap_get(&g.devices, d.h);
    if (!s)
        return NULL;
    if (out_prov)
        *out_prov = s->provider_idx;
    return reg_find_stable(s->provider_idx, s->stable_id);
}

static Device_Slot* device_slot(Mel_Input_Device d) { return (Device_Slot*)mel_slotmap_get(&g.devices, d.h); }

static void device_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("input", "device event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void stream_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("input", "input stream channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_device(Mel_Input_Device_Event ev)
{
    if (g.device_events != NULL)
        mel_event_fire(g.device_events, &ev);
}

Mel_Input_Provider mel_input_provider_register(const Mel_Input_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Input_Provider){ .index = idx, .generation = e.generation };
}

void mel_input_provider_unregister(Mel_Input_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

static const Mel_Input_Provider_Desc* g_test_provider;

void mel_input__set_test_provider(const Mel_Input_Provider_Desc* desc) { g_test_provider = desc; }

void mel_input_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.exec = exec;
    mel_slotmap_init(&g.devices, g.alloc, .item_size = sizeof(Device_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    g.provider_gen = 0;

    g.device_events = mel_event_create(g.alloc, sizeof(Mel_Input_Device_Event), MEL_INPUT_DEVICE_EVENTS_CAP, mel_event_policy_latest(device_overflow, NULL));
    g.device_poll_sub = g.device_events != NULL ? mel_event_subscribe_pull(g.device_events, NULL) : MEL_EVENT_SUB_NULL;
    g.stream_events = mel_event_create(g.alloc, sizeof(Mel_Input_Event), MEL_INPUT_STREAM_EVENTS_CAP, mel_event_policy_latest(stream_overflow, NULL));
    g.stream_poll_sub = g.stream_events != NULL ? mel_event_subscribe_pull(g.stream_events, NULL) : MEL_EVENT_SUB_NULL;

    g.initialized = true;

    if (g_test_provider != NULL)
        mel_input_provider_register(g_test_provider);
    else
        mel_input__register_host_providers();

    mel_input_refresh();
}

void mel_input_init(const Mel_Alloc* alloc) { mel_input_init_ex(alloc, NULL); }

void mel_input_shutdown(void)
{
    if (!g.initialized)
        return;
    if (g.device_events != NULL)
        mel_event_unsubscribe(g.device_events, g.device_poll_sub);
    mel_event_destroy(g.device_events);
    if (g.stream_events != NULL)
        mel_event_unsubscribe(g.stream_events, g.stream_poll_sub);
    mel_event_destroy(g.stream_events);
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.devices);
    memset(&g, 0, sizeof g);
    g_test_provider = NULL;
}

u32 mel_input_refresh(void)
{
    if (!g.initialized)
        mel_input_init(NULL);

    for (usize i = 0; i < g.registry.count; i++)
        g.registry.items[i].seen = false;

    for (u32 p = 0; p < g.providers.count; p++)
    {
        Provider_Entry* prov = provider_get(p);
        if (!prov || prov->desc.enumerate == NULL)
            continue;

        Mel_Input_Raw raw[64];
        u32           n = prov->desc.enumerate(prov->desc.user, raw, 64);
        for (u32 i = 0; i < n; i++)
        {
            Reg_Entry* e = reg_find_stable(p, raw[i].stable_id);
            if (e)
            {
                e->seen = true;
                Device_Slot* s = device_slot((Mel_Input_Device){ e->handle });
                if (!s)
                    continue;
                u32 fields = mel_input_events__changed_fields(&s->desc, &raw[i].desc);
                if (fields == 0)
                    continue;
                s->desc = raw[i].desc;
                fire_device((Mel_Input_Device_Event){ .kind = MEL_INPUT_DEVICE_EVENT_CHANGED, .device = { e->handle }, .changed_fields = fields });
                continue;
            }

            Device_Slot        slot = { .provider_idx = p, .stable_id = raw[i].stable_id, .desc = raw[i].desc };
            Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
            Reg_Entry          re = { .provider_idx = p, .stable_id = raw[i].stable_id, .handle = h, .seen = true };
            mel_array_push(&g.registry, re);
            fire_device((Mel_Input_Device_Event){ .kind = MEL_INPUT_DEVICE_EVENT_ADDED, .device = { h } });
        }
    }

    for (usize i = 0; i < g.registry.count;)
    {
        if (g.registry.items[i].seen)
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.registry.items[i].handle;
        mel_slotmap_remove(&g.devices, h);
        fire_device((Mel_Input_Device_Event){ .kind = MEL_INPUT_DEVICE_EVENT_REMOVED, .device = { h } });
        g.registry.items[i] = g.registry.items[g.registry.count - 1];
        g.registry.count--;
    }

    return mel_slotmap_count(&g.devices);
}

u32 mel_input_count(void)
{
    if (!g.initialized)
        return 0;
    return mel_slotmap_count(&g.devices);
}

u32 mel_input_list(Mel_Input_Device* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = (u32)(g.registry.count < cap ? g.registry.count : cap);
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Input_Device){ g.registry.items[i].handle };
    return n;
}

Mel_Input_Describe_Result mel_input_describe(Mel_Input_Device d)
{
    Mel_Input_Describe_Result r = { 0 };
    if (!g.initialized || !mel_slotmap_alive(&g.devices, d.h))
    {
        mel_log_error("input", "describe on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_INPUT_INVALID_HANDLE | MEL_INPUT_ERROR;
        return r;
    }
    Device_Slot* s = device_slot(d);
    r.value = s->desc;
    r.status = MEL_INPUT_OK;
    return r;
}

bool mel_input_alive(Mel_Input_Device d) { return g.initialized && mel_slotmap_alive(&g.devices, d.h); }

bool mel_input_equal(Mel_Input_Device a, Mel_Input_Device b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

void* mel_input_native(Mel_Input_Device d)
{
    u32        prov_idx = 0;
    Reg_Entry* e = reg_find_by_handle(d, &prov_idx);
    if (!e)
        return NULL;
    Provider_Entry* prov = provider_get(prov_idx);
    if (!prov || prov->desc.native == NULL)
        return NULL;
    return prov->desc.native(prov->desc.user, e->stable_id);
}

Mel_Event* mel_input_event_channel(void) { return g.initialized ? g.stream_events : NULL; }

bool mel_input__stable_id(Mel_Input_Device d, u64* out_id)
{
    if (!mel_input_alive(d))
        return false;
    Device_Slot* s = device_slot(d);
    *out_id = s->stable_id;
    return true;
}

u32 mel_input_poll_events(Mel_Input_Device_Event* out, u32 cap)
{
    if (!g.initialized || g.device_events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.device_events, g.device_poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Input_Subscription mel_input_subscribe(Mel_Executor* exec, Mel_Input_Device_Event_Callback cb, void* user)
{
    if (!g.initialized || g.device_events == NULL)
    {
        mel_log_error("input", "subscribe before init; no channel");
        return MEL_INPUT_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    if (target == NULL)
    {
        mel_log_error("input", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_INPUT_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.device_events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Input_Subscription){ sub.handle };
}

void mel_input_unsubscribe(Mel_Input_Subscription sub)
{
    if (!g.initialized || g.device_events == NULL)
        return;
    mel_event_unsubscribe(g.device_events, (Mel_Event_Sub){ sub.handle });
}

static void fire_stream(Mel_Input_Event it)
{
    if (g.stream_events != NULL)
        mel_event_fire(g.stream_events, &it);
}

void mel_input_sink_key(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Key_Event* ev)
{
    MEL_UNUSED(sink);
    MEL_UNUSED(stable_id);
    fire_stream((Mel_Input_Event){ .kind = MEL_INPUT_EVENT_KEY, .as.key = *ev });
}

void mel_input_sink_text(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Text_Event* ev)
{
    MEL_UNUSED(sink);
    MEL_UNUSED(stable_id);
    fire_stream((Mel_Input_Event){ .kind = MEL_INPUT_EVENT_TEXT, .as.text = *ev });
}

void mel_input_sink_mouse(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Mouse_Event* ev)
{
    MEL_UNUSED(sink);
    MEL_UNUSED(stable_id);
    fire_stream((Mel_Input_Event){ .kind = MEL_INPUT_EVENT_MOUSE, .as.mouse = *ev });
}

void mel_input_sink_touch(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Touch_Event* ev)
{
    MEL_UNUSED(sink);
    MEL_UNUSED(stable_id);
    fire_stream((Mel_Input_Event){ .kind = MEL_INPUT_EVENT_TOUCH, .as.touch = *ev });
}

void mel_input_sink_pen(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Pen_Event* ev)
{
    MEL_UNUSED(sink);
    MEL_UNUSED(stable_id);
    fire_stream((Mel_Input_Event){ .kind = MEL_INPUT_EVENT_PEN, .as.pen = *ev });
}

void mel_input_pump(void)
{
    if (!g.initialized)
        return;
    Mel_Input_Sink* sink = &g_sink;
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.pump != NULL)
            p->desc.pump(p->desc.user, sink);
    }
}

u32 mel_input_poll(Mel_Input_Event* out, u32 cap)
{
    if (!g.initialized || g.stream_events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.stream_events, g.stream_poll_sub, &out[n]); n++)
        ;
    return n;
}

static Provider_Entry* device_provider(Mel_Input_Device d, u64* out_stable)
{
    Device_Slot* s = device_slot(d);
    if (!s)
        return NULL;
    if (out_stable)
        *out_stable = s->stable_id;
    return provider_get(s->provider_idx);
}

bool mel_keyboard_key_down(Mel_Input_Device d, Mel_Scancode sc)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.key_down == NULL)
        return false;
    return p->desc.key_down(p->desc.user, sid, sc);
}

u32 mel_keyboard_modifiers(Mel_Input_Device d)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.modifiers == NULL)
        return 0;
    return p->desc.modifiers(p->desc.user, sid);
}

Mel_Scancode mel_keyboard_scancode_from_keycode(Mel_Input_Device d, Mel_Keycode kc)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.scancode_from_keycode == NULL)
        return MEL_SCANCODE_UNKNOWN;
    return p->desc.scancode_from_keycode(p->desc.user, sid, kc);
}

Mel_Keycode mel_keyboard_keycode_from_scancode(Mel_Input_Device d, Mel_Scancode sc)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.keycode_from_scancode == NULL)
        return MEL_KEYCODE_UNKNOWN;
    return p->desc.keycode_from_scancode(p->desc.user, sid, sc);
}

str8 mel_keyboard_scancode_name(Mel_Scancode sc) { return Mel_Scancode_to_string(sc); }

str8 mel_keyboard_key_name(Mel_Input_Device d, Mel_Keycode kc, char* buf, usize buf_size)
{
    MEL_UNUSED(d);
    if (buf == NULL || buf_size == 0)
        return STR8_EMPTY;
    if (kc == MEL_KEYCODE_UNKNOWN || (kc & MEL_KEYCODE_EXTENDED_BIT))
        return STR8_EMPTY;
    usize n = 0;
    if (kc < 0x80)
    {
        if (buf_size >= 2)
        {
            buf[0] = (char)kc;
            buf[1] = 0;
            n = 1;
        }
    }
    else
    {
        u32 cp = kc;
        if (cp < 0x800 && buf_size >= 3)
        {
            buf[0] = (char)(0xC0 | (cp >> 6));
            buf[1] = (char)(0x80 | (cp & 0x3F));
            buf[2] = 0;
            n = 2;
        }
        else if (cp < 0x10000 && buf_size >= 4)
        {
            buf[0] = (char)(0xE0 | (cp >> 12));
            buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (cp & 0x3F));
            buf[3] = 0;
            n = 3;
        }
        else if (buf_size >= 5)
        {
            buf[0] = (char)(0xF0 | (cp >> 18));
            buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[3] = (char)(0x80 | (cp & 0x3F));
            buf[4] = 0;
            n = 4;
        }
    }
    return (str8){ (u8*)buf, (size)n };
}

Mel_Mouse_State mel_mouse_state(Mel_Input_Device d)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.mouse_state == NULL)
        return (Mel_Mouse_State){ .device = d };
    return p->desc.mouse_state(p->desc.user, sid);
}

Mel_Input_Status mel_mouse_set_relative(Mel_Input_Device d, bool enable)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.mouse_set_relative == NULL)
        return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
    return p->desc.mouse_set_relative(p->desc.user, sid, enable);
}

bool mel_mouse_relative(Mel_Input_Device d) { return mel_mouse_state(d).relative; }

Mel_Input_Status mel_mouse_capture(bool enable)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.mouse_capture != NULL)
            return p->desc.mouse_capture(p->desc.user, enable);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
}

bool mel_mouse_captured(void)
{
    Mel_Input_Device devs[16];
    u32              n = mel_input_list(devs, 16);
    for (u32 i = 0; i < n; i++)
        if (mel_mouse_state(devs[i]).captured)
            return true;
    return false;
}

Mel_Input_Status mel_mouse_warp(f32 x, f32 y)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.mouse_warp != NULL)
            return p->desc.mouse_warp(p->desc.user, 0, x, y, false);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_WARP_UNAVAILABLE;
}

Mel_Input_Status mel_mouse_warp_global(f32 x, f32 y)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.mouse_warp != NULL)
            return p->desc.mouse_warp(p->desc.user, 0, x, y, true);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_WARP_UNAVAILABLE;
}

Mel_Input_Status mel_mouse_confine(Mel_Mouse_Rect rect)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.mouse_confine != NULL)
            return p->desc.mouse_confine(p->desc.user, &rect);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_CONFINE_UNAVAILABLE;
}

Mel_Input_Status mel_mouse_unconfine(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.mouse_confine != NULL)
            return p->desc.mouse_confine(p->desc.user, NULL);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_CONFINE_UNAVAILABLE;
}

static Provider_Entry* first_provider_with_cursor(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.cursor_create_system != NULL)
            return p;
    }
    return NULL;
}

Mel_Cursor mel_cursor_create_system(Mel_Cursor_Shape shape)
{
    Provider_Entry* p = first_provider_with_cursor();
    if (!p)
        return MEL_CURSOR_NULL;
    return p->desc.cursor_create_system(p->desc.user, shape);
}

Mel_Cursor mel_cursor_create_opt(const Mel_Alloc* alloc, Mel_Cursor_Opt opt)
{
    MEL_UNUSED(alloc);
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.cursor_create_custom != NULL)
            return p->desc.cursor_create_custom(p->desc.user, &opt);
    }
    return MEL_CURSOR_NULL;
}

void mel_cursor_destroy(Mel_Cursor c)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.cursor_destroy != NULL)
        {
            p->desc.cursor_destroy(p->desc.user, c);
            return;
        }
    }
}

Mel_Input_Status mel_cursor_set(Mel_Cursor c)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.cursor_set != NULL)
            return p->desc.cursor_set(p->desc.user, c);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
}

Mel_Input_Status mel_cursor_show(bool visible)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.cursor_show != NULL)
            return p->desc.cursor_show(p->desc.user, visible);
    }
    return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
}

bool mel_cursor_visible(void) { return true; }

Mel_Touch_State mel_touch_state(Mel_Input_Device d)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.touch_state == NULL)
        return (Mel_Touch_State){ .device = d };
    return p->desc.touch_state(p->desc.user, sid);
}

u32 mel_touch_fingers(Mel_Input_Device d, Mel_Touch_Finger* out, u32 cap)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.touch_fingers == NULL)
        return 0;
    return p->desc.touch_fingers(p->desc.user, sid, out, cap);
}

Mel_Pen_State mel_pen_state(Mel_Input_Device d)
{
    u64             sid = 0;
    Provider_Entry* p = device_provider(d, &sid);
    if (!p || p->desc.pen_state == NULL)
        return (Mel_Pen_State){ .device = d };
    return p->desc.pen_state(p->desc.user, sid);
}

typedef struct
{
    u32  input_type;
    bool active;
    bool osk_visible;
} Text_Input;

static Text_Input g_text;

Mel_Input_Status mel_input_text_start_opt(Mel_Input_Text_Opt opt)
{
    Mel_Input_Status st = MEL_INPUT_OK;
    bool             handled = false;
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.text_start != NULL)
        {
            st = p->desc.text_start(p->desc.user, &opt);
            handled = true;
            break;
        }
    }
    if (!handled)
        return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
    g_text.active = true;
    g_text.input_type = opt.input_type;
    return st;
}

void mel_input_text_stop(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.text_stop != NULL)
        {
            p->desc.text_stop(p->desc.user);
            break;
        }
    }
    g_text.active = false;
}

bool mel_input_text_active(void) { return g_text.active; }

Mel_Input_Status mel_input_text_set_area(Mel_Input_Rect area)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.text_set_area != NULL)
            return p->desc.text_set_area(p->desc.user, area);
    }
    return MEL_INPUT_WARNED | MEL_INPUT_AREA_IGNORED;
}

Mel_Input_Status mel_input_osk_show(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.osk_show != NULL)
        {
            Mel_Input_Status st = p->desc.osk_show(p->desc.user);
            g_text.osk_visible = !mel_input_status_failed(st);
            return st;
        }
    }
    return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
}

Mel_Input_Status mel_input_osk_hide(void)
{
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* p = provider_get(i);
        if (p && p->desc.osk_hide != NULL)
        {
            Mel_Input_Status st = p->desc.osk_hide(p->desc.user);
            if (!mel_input_status_failed(st))
                g_text.osk_visible = false;
            return st;
        }
    }
    return MEL_INPUT_ERROR | MEL_INPUT_UNSUPPORTED;
}

bool mel_input_osk_visible(void) { return g_text.osk_visible; }
