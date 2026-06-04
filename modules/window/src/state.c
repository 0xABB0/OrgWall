#include "window_internal.h"

#include <executor/executor.h>
#include <collection/list.h>
#include <debug/assert.h>

typedef struct
{
    Mel_Future            future;
    Mel_Window_Icc_Result result;
    Mel_Executor*         deliver;
    Mel_Task              deliver_task;
    u32                   op_index;
    u32                   op_generation;
    bool                  cancelled;
} Mel_Window_Icc_Op;

static Mel_SlotMap g_icc_ops;
static bool        g_icc_ops_inited;

static void mel_window__icc_ops_ensure(void)
{
    if (g_icc_ops_inited)
        return;
    mel_slotmap_init(&g_icc_ops, mel_window__alloc(), .item_size = sizeof(Mel_Window_Icc_Op*), .initial_capacity = 4);
    g_icc_ops_inited = true;
}

static Mel_Window_Status mel_window__guard(Mel_Window w, Mel_Window_Node** out)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return MEL_WINDOW_ERROR | MEL_WINDOW_INVALID_HANDLE;
    *out = n;
    return MEL_WINDOW_OK;
}

static Mel_Window_Status mel_window__dispatch(Mel_Window w, bool (*invoke)(const Mel_Window_Backend_Ops*, Mel_Window_Node*))
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    const Mel_Window_Backend_Ops* ops = n->ops;
    if (!ops || !invoke(ops, n))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_min_size(Mel_Window w, i32 min_w, i32 min_h)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->min_w = min_w < 0 ? 0 : min_w;
    n->min_h = min_h < 0 ? 0 : min_h;
    if (!n->ops || !n->ops->set_min_size || !n->ops->set_min_size(n, n->min_w, n->min_h))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_max_size(Mel_Window w, i32 max_w, i32 max_h)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->max_w = max_w < 0 ? 0 : max_w;
    n->max_h = max_h < 0 ? 0 : max_h;
    if (!n->ops || !n->ops->set_max_size || !n->ops->set_max_size(n, n->max_w, n->max_h))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_aspect_ratio(Mel_Window w, f32 min_ratio, f32 max_ratio)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (min_ratio < 0.0f || max_ratio < 0.0f || (max_ratio > 0.0f && min_ratio > max_ratio))
        return MEL_WINDOW_ERROR | MEL_WINDOW_REJECTED;
    n->aspect_min = min_ratio;
    n->aspect_max = max_ratio;
    if (!n->ops || !n->ops->set_aspect || !n->ops->set_aspect(n, min_ratio, max_ratio))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_fullscreen(Mel_Window w, u32 fullscreen_flags)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->fullscreen_flags = fullscreen_flags;
    if (!n->ops || !n->ops->set_fullscreen || !n->ops->set_fullscreen(n, fullscreen_flags))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_fullscreen_mode(Mel_Window w, Mel_Window_Video_Mode mode)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (mode.width_px == 0 || mode.height_px == 0)
        return MEL_WINDOW_ERROR | MEL_WINDOW_REJECTED;
    if (!n->ops || !n->ops->set_fullscreen_mode || !n->ops->set_fullscreen_mode(n, mode))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Video_Mode_Result mel_window_get_fullscreen_mode(Mel_Window w)
{
    Mel_Window_Video_Mode_Result r = { 0 };
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
    {
        r.status = g;
        return r;
    }
    if (!n->ops || !n->ops->get_fullscreen_mode || !n->ops->get_fullscreen_mode(n, &r.value))
    {
        r.status = MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
        return r;
    }
    r.status = MEL_WINDOW_OK;
    return r;
}

Mel_Window_Status mel_window_set_opacity(Mel_Window w, f32 opacity)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    Mel_Window_Status st = MEL_WINDOW_OK;
    if (opacity < 0.0f)
    {
        opacity = 0.0f;
        st = MEL_WINDOW_WARNED | MEL_WINDOW_CLAMPED;
    }
    else if (opacity > 1.0f)
    {
        opacity = 1.0f;
        st = MEL_WINDOW_WARNED | MEL_WINDOW_CLAMPED;
    }
    n->opacity = opacity;
    if (!n->ops || !n->ops->set_opacity || !n->ops->set_opacity(n, opacity))
        return st | MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return st;
}

f32 mel_window_get_opacity(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    return n ? n->opacity : 0.0f;
}

Mel_Window_Status mel_window_set_always_on_top(Mel_Window w, bool on)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->always_on_top = on;
    if (!n->ops || !n->ops->set_always_on_top || !n->ops->set_always_on_top(n, on))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_borderless(Mel_Window w, bool borderless)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->borderless = borderless;
    if (!n->ops || !n->ops->set_borderless || !n->ops->set_borderless(n, borderless))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_resizable(Mel_Window w, bool resizable)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->resizable = resizable;
    if (!n->ops || !n->ops->set_resizable || !n->ops->set_resizable(n, resizable))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_icon(Mel_Window w, const u8* rgba, i32 width, i32 height)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (!rgba || width <= 0 || height <= 0)
        return MEL_WINDOW_ERROR | MEL_WINDOW_REJECTED;
    if (!n->ops || !n->ops->set_icon || !n->ops->set_icon(n, rgba, width, height))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_modal(Mel_Window w, bool modal)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->modal = modal;
    if (!n->ops || !n->ops->set_modal || !n->ops->set_modal(n, modal))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_parent(Mel_Window w, Mel_Window parent)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    Mel_Window_Node* pn = NULL;
    if (!mel_window_is_none(parent))
    {
        pn = mel_window__node(parent);
        if (!pn)
            return MEL_WINDOW_ERROR | MEL_WINDOW_REJECTED;
    }
    n->parent = parent;
    if (!n->ops || !n->ops->set_parent || !n->ops->set_parent(n, pn))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window mel_window_get_parent(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    return n ? n->parent : MEL_WINDOW_NONE;
}

Mel_Window_Status mel_window_set_hit_test(Mel_Window w, Mel_Window_Hit_Test cb, void* user)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->hit_test = cb;
    n->hit_test_user = user;
    return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
}

Mel_Window_Status mel_window_set_shape(Mel_Window w, const u8* alpha_mask, i32 width, i32 height)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (alpha_mask && (width <= 0 || height <= 0))
        return MEL_WINDOW_ERROR | MEL_WINDOW_REJECTED;
    if (!n->ops || !n->ops->set_shape || !n->ops->set_shape(n, alpha_mask, width, height))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_mouse_grab(Mel_Window w, bool grab)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->mouse_grab = grab;
    if (!n->ops || !n->ops->set_mouse_grab || !n->ops->set_mouse_grab(n, grab))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_keyboard_grab(Mel_Window w, bool grab)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->keyboard_grab = grab;
    if (!n->ops || !n->ops->set_keyboard_grab || !n->ops->set_keyboard_grab(n, grab))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_mouse_rect(Mel_Window w, Mel_Window_Rect rect_px)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (rect_px.w < 0 || rect_px.h < 0)
        return MEL_WINDOW_ERROR | MEL_WINDOW_REJECTED;
    n->mouse_rect = rect_px;
    n->have_mouse_rect = (rect_px.w > 0 && rect_px.h > 0);
    if (!n->ops || !n->ops->set_mouse_rect || !n->ops->set_mouse_rect(n, rect_px))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_progress_state(Mel_Window w, u32 progress_state)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    n->progress_state = progress_state;
    if (!n->ops || !n->ops->set_progress_state || !n->ops->set_progress_state(n, progress_state))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Status mel_window_set_progress_value(Mel_Window w, f32 value)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    Mel_Window_Status st = MEL_WINDOW_OK;
    if (value < 0.0f)
    {
        value = 0.0f;
        st = MEL_WINDOW_WARNED | MEL_WINDOW_CLAMPED;
    }
    else if (value > 1.0f)
    {
        value = 1.0f;
        st = MEL_WINDOW_WARNED | MEL_WINDOW_CLAMPED;
    }
    n->progress_value = value;
    if (!n->ops || !n->ops->set_progress_value || !n->ops->set_progress_value(n, value))
        return st | MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return st;
}

static u32 mel_window__compose_flags(Mel_Window_Node* n)
{
    u32 flags = 0;
    if (n->ops && n->ops->live_flags)
        flags = n->ops->live_flags(n);
    if (n->always_on_top)
        flags |= MEL_WINDOW_STATE_ALWAYS_TOP;
    if (n->borderless)
        flags |= MEL_WINDOW_STATE_BORDERLESS;
    if (n->resizable)
        flags |= MEL_WINDOW_STATE_RESIZABLE;
    if (n->mouse_grab)
        flags |= MEL_WINDOW_STATE_MOUSE_GRAB;
    if (n->keyboard_grab)
        flags |= MEL_WINDOW_STATE_KEY_GRAB;
    if (n->modal)
        flags |= MEL_WINDOW_STATE_MODAL;
    if (n->transparent)
        flags |= MEL_WINDOW_STATE_TRANSPARENT;
    if (n->fullscreen_flags != MEL_WINDOW_FULLSCREEN_OFF)
        flags |= MEL_WINDOW_STATE_FULLSCREEN;
    return flags;
}

Mel_Window_State_Result mel_window_query_state(Mel_Window w)
{
    Mel_Window_State_Result r = { 0 };
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
    {
        r.status = g;
        return r;
    }
    r.value.flags = mel_window__compose_flags(n);
    r.value.bounds_px = (Mel_Window_Rect){ n->x, n->y, n->w, n->h };
    r.value.opacity = n->opacity;
    r.value.scale = n->scale;
    r.value.progress_state = n->progress_state;
    r.value.progress_value = n->progress_value;
    if (n->ops && n->ops->safe_area)
        n->ops->safe_area(n, &r.value.safe_area_px);
    else
        r.value.safe_area_px = (Mel_Window_Rect){ 0, 0, n->w, n->h };
    if (n->ops && n->ops->pixel_format)
        n->ops->pixel_format(n, &r.value.pixel_format_flags);
    r.status = MEL_WINDOW_OK;
    return r;
}

Mel_Window_Rect mel_window_safe_area(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return (Mel_Window_Rect){ 0 };
    Mel_Window_Rect rect = { 0, 0, n->w, n->h };
    if (n->ops && n->ops->safe_area)
        n->ops->safe_area(n, &rect);
    return rect;
}

void mel_window_get_position(Mel_Window w, i32* out_x, i32* out_y)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (out_x)
        *out_x = n ? n->x : 0;
    if (out_y)
        *out_y = n ? n->y : 0;
}

u32 mel_window_pixel_format(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    u32 flags = MEL_WINDOW_PIXEL_UNKNOWN;
    if (n && n->ops && n->ops->pixel_format)
        n->ops->pixel_format(n, &flags);
    return flags;
}

static bool mel_window__inv_maximize(const Mel_Window_Backend_Ops* o, Mel_Window_Node* n) { return o->maximize && o->maximize(n); }
static bool mel_window__inv_minimize(const Mel_Window_Backend_Ops* o, Mel_Window_Node* n) { return o->minimize && o->minimize(n); }
static bool mel_window__inv_restore(const Mel_Window_Backend_Ops* o, Mel_Window_Node* n) { return o->restore && o->restore(n); }
static bool mel_window__inv_raise(const Mel_Window_Backend_Ops* o, Mel_Window_Node* n) { return o->raise && o->raise(n); }

Mel_Window_Status mel_window_maximize(Mel_Window w) { return mel_window__dispatch(w, mel_window__inv_maximize); }
Mel_Window_Status mel_window_minimize(Mel_Window w) { return mel_window__dispatch(w, mel_window__inv_minimize); }
Mel_Window_Status mel_window_restore(Mel_Window w) { return mel_window__dispatch(w, mel_window__inv_restore); }
Mel_Window_Status mel_window_raise(Mel_Window w) { return mel_window__dispatch(w, mel_window__inv_raise); }

Mel_Window_Status mel_window_flash(Mel_Window w, u32 flash_flags)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (!n->ops || !n->ops->flash || !n->ops->flash(n, flash_flags))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window mel_window_by_id(u64 native_id)
{
    u32 count = mel_window__node_count();
    for (u32 i = 0; i < count; i++)
    {
        Mel_Window_Node* n = mel_window__node_dense(i);
        if (!n || !n->native || !n->ops || !n->ops->native_id)
            continue;
        if (n->ops->native_id(n) == native_id)
            return n->self;
    }
    return MEL_WINDOW_NONE;
}

u32 mel_window_enumerate_all(Mel_Window* out, u32 cap)
{
    u32 count = mel_window__node_count();
    u32 written = 0;
    for (u32 i = 0; i < count; i++)
    {
        Mel_Window_Node* n = mel_window__node_dense(i);
        if (!n || mel_window_is_none(n->self))
            continue;
        if (out && written < cap)
            out[written] = n->self;
        written++;
    }
    return written;
}

Mel_Window_Surface_Result mel_window_get_surface(Mel_Window w)
{
    Mel_Window_Surface_Result r = { 0 };
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
    {
        r.status = g;
        return r;
    }
    if (!n->ops || !n->ops->get_surface || !n->ops->get_surface(n, &r.value))
    {
        r.status = MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
        return r;
    }
    r.status = MEL_WINDOW_OK;
    return r;
}

Mel_Window_Status mel_window_present_surface(Mel_Window w)
{
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
        return g;
    if (!n->ops || !n->ops->present_surface || !n->ops->present_surface(n))
        return MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
    return MEL_WINDOW_OK;
}

Mel_Window_Icc_Result mel_window_icc_profile(Mel_Window w)
{
    Mel_Window_Icc_Result r = { 0 };
    Mel_Window_Node* n = NULL;
    Mel_Window_Status g = mel_window__guard(w, &n);
    if (g != MEL_WINDOW_OK)
    {
        r.status = g;
        return r;
    }
    if (!n->ops || !n->ops->icc_profile || !n->ops->icc_profile(n, &r.value))
    {
        r.status = MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE;
        return r;
    }
    r.status = MEL_WINDOW_OK;
    return r;
}

const Mel_Window_Icc_Result* mel_window_icc_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Window_Icc_Op* op = (Mel_Window_Icc_Op*)f;
    return &op->result;
}

void mel_window_icc_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Window_Icc_Op* op = (Mel_Window_Icc_Op*)f;
    if (g_icc_ops_inited && op->op_generation != 0)
    {
        Mel_SlotMap_Handle h = { .index = op->op_index, .generation = op->op_generation };
        mel_slotmap_remove(&g_icc_ops, h);
    }
    mel_dealloc(mel_window__alloc(), op);
}

static void mel_window__icc_deliver(Mel_Task* t)
{
    Mel_Window_Icc_Op* op = mel_container_of(t, Mel_Window_Icc_Op, deliver_task);
    Mel_Window_Status  status = op->result.status;
    if (op->cancelled)
    {
        if (op->result.value.data)
            mel_dealloc(mel_window__alloc(), (void*)op->result.value.data);
        op->result.value = (Mel_Window_Icc_Profile){ 0 };
        status = MEL_WINDOW_ERROR | MEL_WINDOW_CANCELLED;
    }
    op->result.status = status;
    if (op->cancelled)
    {
        mel_future_cancel(&op->future);
        return;
    }
    Mel_Future_Status fs = mel_window_status_failed(status) ? MEL_FUTURE_ERROR : (mel_window_status_warned(status) ? MEL_FUTURE_WARNED : MEL_FUTURE_OK);
    mel_future_resolve(&op->future, &op->result, fs);
}

Mel_Future* mel_window_fetch_icc_opt(Mel_Window w, Mel_Window_Icc_Opt opt)
{
    const Mel_Alloc* alloc = mel_window__alloc();
    Mel_Window_Icc_Op* op = (Mel_Window_Icc_Op*)mel_alloc(alloc, sizeof(*op));
    mel_assert(op != NULL);
    *op = (Mel_Window_Icc_Op){ 0 };
    mel_future_init(&op->future, NULL, alloc);
    mel_task_init(&op->deliver_task, mel_window__icc_deliver);
    op->deliver = opt.deliver ? opt.deliver : mel_executor_inline();

    mel_window__icc_ops_ensure();
    Mel_Window_Icc_Op* slot_val = op;
    Mel_SlotMap_Handle h = mel_slotmap_insert(&g_icc_ops, &slot_val);
    op->op_index = h.index;
    op->op_generation = h.generation;
    if (opt.out_op)
        *opt.out_op = (Mel_Window_Op){ .index = h.index, .generation = h.generation };

    Mel_Window_Icc_Result r = mel_window_icc_profile(w);
    op->result = r;

    mel_executor_submit(op->deliver, &op->deliver_task);
    return &op->future;
}

bool mel_window_cancel(Mel_Window_Op op)
{
    if (!g_icc_ops_inited || !mel_window_op_valid(op))
        return false;
    Mel_SlotMap_Handle h = { .index = op.index, .generation = op.generation };
    Mel_Window_Icc_Op** slot = (Mel_Window_Icc_Op**)mel_slotmap_get(&g_icc_ops, h);
    if (!slot || !*slot)
        return false;
    (*slot)->cancelled = true;
    return true;
}
