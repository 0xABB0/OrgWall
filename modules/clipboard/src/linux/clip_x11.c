#include "clip_linux.h"

#include <allocator/allocator.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <dlfcn.h>
#include <string.h>
#include <time.h>

typedef u32 xcb_window_t;
typedef u32 xcb_atom_t;
typedef u32 xcb_timestamp_t;
typedef u32 xcb_visualid_t;
typedef u32 xcb_colormap_t;

typedef struct xcb_connection_t xcb_connection_t;
typedef struct xcb_setup_t      xcb_setup_t;

typedef struct
{
    u8  response_type;
    u8  pad0;
    u16 sequence;
    u32 pad[7];
    u32 full_sequence;
} xcb_generic_event_t;

typedef struct
{
    u8              response_type;
    u8              pad0;
    u16             sequence;
    xcb_timestamp_t time;
    xcb_window_t    owner;
    xcb_window_t    requestor;
    xcb_atom_t      selection;
    xcb_atom_t      target;
    xcb_atom_t      property;
} xcb_selection_request_event_t;

typedef struct
{
    u8              response_type;
    u8              pad0;
    u16             sequence;
    xcb_timestamp_t time;
    xcb_window_t    requestor;
    xcb_atom_t      selection;
    xcb_atom_t      target;
    xcb_atom_t      property;
} xcb_selection_notify_event_t;

typedef struct
{
    u8              response_type;
    u8              pad0;
    u16             sequence;
    xcb_timestamp_t time;
    xcb_window_t    owner;
    xcb_atom_t      selection;
} xcb_selection_clear_event_t;

typedef struct
{
    xcb_window_t   root;
    xcb_colormap_t default_colormap;
    u32            white_pixel;
    u32            black_pixel;
    u32            current_input_masks;
    u16            width_in_pixels;
    u16            height_in_pixels;
    u16            width_in_millimeters;
    u16            height_in_millimeters;
    u16            min_installed_maps;
    u16            max_installed_maps;
    xcb_visualid_t root_visual;
    u8             backing_stores;
    u8             save_unders;
    u8             root_depth;
    u8             allowed_depths_len;
} xcb_screen_t;

typedef struct
{
    xcb_screen_t* data;
    int           rem;
    int           index;
} xcb_screen_iterator_t;

typedef struct
{
    unsigned int sequence;
} xcb_void_cookie_t;
typedef struct
{
    unsigned int sequence;
} xcb_intern_atom_cookie_t;
typedef struct
{
    unsigned int sequence;
} xcb_get_selection_owner_cookie_t;
typedef struct
{
    unsigned int sequence;
} xcb_get_property_cookie_t;

typedef struct
{
    u8         response_type;
    u8         pad0;
    u16        sequence;
    u32        length;
    xcb_atom_t atom;
} xcb_intern_atom_reply_t;

typedef struct
{
    u8           response_type;
    u8           pad0;
    u16          sequence;
    u32          length;
    xcb_window_t owner;
} xcb_get_selection_owner_reply_t;

typedef struct
{
    u8         response_type;
    u8         format;
    u16        sequence;
    u32        length;
    xcb_atom_t type;
    u32        bytes_after;
    u32        value_len;
    u8         pad0[12];
} xcb_get_property_reply_t;

typedef struct
{
    xcb_connection_t* (*connect)(const char*, int*);
    int (*connection_has_error)(xcb_connection_t*);
    void (*disconnect)(xcb_connection_t*);
    int (*get_file_descriptor)(xcb_connection_t*);
    int (*flush)(xcb_connection_t*);
    u32 (*generate_id)(xcb_connection_t*);
    const xcb_setup_t* (*get_setup)(xcb_connection_t*);
    xcb_screen_iterator_t (*setup_roots_iterator)(const xcb_setup_t*);
    xcb_generic_event_t* (*poll_for_event)(xcb_connection_t*);
    xcb_void_cookie_t (*create_window)(xcb_connection_t*, u8, xcb_window_t, xcb_window_t, i16, i16, u16, u16, u16, u16, xcb_visualid_t, u32, const void*);
    xcb_void_cookie_t (*destroy_window)(xcb_connection_t*, xcb_window_t);
    xcb_void_cookie_t (*change_property)(xcb_connection_t*, u8, xcb_window_t, xcb_atom_t, xcb_atom_t, u8, u32, const void*);
    xcb_void_cookie_t (*set_selection_owner)(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_timestamp_t);
    xcb_get_selection_owner_cookie_t (*get_selection_owner)(xcb_connection_t*, xcb_atom_t);
    xcb_get_selection_owner_reply_t* (*get_selection_owner_reply)(xcb_connection_t*, xcb_get_selection_owner_cookie_t, void*);
    xcb_void_cookie_t (*convert_selection)(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_atom_t, xcb_atom_t, xcb_timestamp_t);
    xcb_get_property_cookie_t (*get_property)(xcb_connection_t*, u8, xcb_window_t, xcb_atom_t, xcb_atom_t, u32, u32);
    xcb_get_property_reply_t* (*get_property_reply)(xcb_connection_t*, xcb_get_property_cookie_t, void*);
    void* (*get_property_value)(const xcb_get_property_reply_t*);
    int (*get_property_value_length)(const xcb_get_property_reply_t*);
    xcb_void_cookie_t (*send_event)(xcb_connection_t*, u8, xcb_window_t, u32, const char*);
    xcb_intern_atom_cookie_t (*intern_atom)(xcb_connection_t*, u8, u16, const char*);
    xcb_intern_atom_reply_t* (*intern_atom_reply)(xcb_connection_t*, xcb_intern_atom_cookie_t, void*);
} Xcb_Api;

#define XCB_NONE                       0
#define XCB_WINDOW_CLASS_INPUT_OUTPUT  1
#define XCB_CW_EVENT_MASK              2048u
#define XCB_EVENT_MASK_PROPERTY_CHANGE 4194304u
#define XCB_PROP_MODE_REPLACE          0
#define XCB_ATOM_NONE                  0
#define XCB_ATOM_PRIMARY               1
#define XCB_ATOM_ATOM                  4
#define XCB_ATOM_STRING                31
#define XCB_CURRENT_TIME               0
#define XCB_SELECTION_NOTIFY           31
#define XCB_SELECTION_REQUEST          30
#define XCB_SELECTION_CLEAR            29
#define XCB_EVENT_MASK_NO_EVENT        0u

typedef struct
{
    str8 bytes;
    bool valid;
} X11_Owned;

typedef struct
{
    void*               lib;
    Xcb_Api             api;
    xcb_connection_t*   conn;
    xcb_window_t        window;
    xcb_window_t        root;
    Mel_Reactor_Source* source;
    Mel_Reactor_Poll    poll;

    xcb_atom_t clipboard;
    xcb_atom_t primary;
    xcb_atom_t targets;
    xcb_atom_t utf8_string;
    xcb_atom_t prop;

    X11_Owned clip_owned;
    X11_Owned prim_owned;
    u64       clip_seq;
    u64       prim_seq;
    bool      ok;
} X11_State;

static X11_State g_x;

static void* x11_sym(void* lib, const char* name)
{
    void* s = dlsym(lib, name);
    if (!s)
        mel_log_error("clipboard", "x11 backend: symbol '%s' missing from libxcb", name);
    return s;
}

static bool x11_load(X11_State* x)
{
    x->lib = dlopen("libxcb.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!x->lib)
        x->lib = dlopen("libxcb.so", RTLD_NOW | RTLD_GLOBAL);
    if (!x->lib)
        return false;
    Xcb_Api* a = &x->api;
    a->connect = (xcb_connection_t * (*)(const char*, int*)) x11_sym(x->lib, "xcb_connect");
    a->connection_has_error = (int (*)(xcb_connection_t*))x11_sym(x->lib, "xcb_connection_has_error");
    a->disconnect = (void (*)(xcb_connection_t*))x11_sym(x->lib, "xcb_disconnect");
    a->get_file_descriptor = (int (*)(xcb_connection_t*))x11_sym(x->lib, "xcb_get_file_descriptor");
    a->flush = (int (*)(xcb_connection_t*))x11_sym(x->lib, "xcb_flush");
    a->generate_id = (u32 (*)(xcb_connection_t*))x11_sym(x->lib, "xcb_generate_id");
    a->get_setup = (const xcb_setup_t* (*)(xcb_connection_t*))x11_sym(x->lib, "xcb_get_setup");
    a->setup_roots_iterator = (xcb_screen_iterator_t (*)(const xcb_setup_t*))x11_sym(x->lib, "xcb_setup_roots_iterator");
    a->poll_for_event = (xcb_generic_event_t * (*)(xcb_connection_t*)) x11_sym(x->lib, "xcb_poll_for_event");
    a->create_window = (xcb_void_cookie_t (*)(xcb_connection_t*, u8, xcb_window_t, xcb_window_t, i16, i16, u16, u16, u16, u16, xcb_visualid_t, u32, const void*))x11_sym(x->lib, "xcb_create_window");
    a->destroy_window = (xcb_void_cookie_t (*)(xcb_connection_t*, xcb_window_t))x11_sym(x->lib, "xcb_destroy_window");
    a->change_property = (xcb_void_cookie_t (*)(xcb_connection_t*, u8, xcb_window_t, xcb_atom_t, xcb_atom_t, u8, u32, const void*))x11_sym(x->lib, "xcb_change_property");
    a->set_selection_owner = (xcb_void_cookie_t (*)(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_timestamp_t))x11_sym(x->lib, "xcb_set_selection_owner");
    a->get_selection_owner = (xcb_get_selection_owner_cookie_t (*)(xcb_connection_t*, xcb_atom_t))x11_sym(x->lib, "xcb_get_selection_owner");
    a->get_selection_owner_reply = (xcb_get_selection_owner_reply_t * (*)(xcb_connection_t*, xcb_get_selection_owner_cookie_t, void*)) x11_sym(x->lib, "xcb_get_selection_owner_reply");
    a->convert_selection = (xcb_void_cookie_t (*)(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_atom_t, xcb_atom_t, xcb_timestamp_t))x11_sym(x->lib, "xcb_convert_selection");
    a->get_property = (xcb_get_property_cookie_t (*)(xcb_connection_t*, u8, xcb_window_t, xcb_atom_t, xcb_atom_t, u32, u32))x11_sym(x->lib, "xcb_get_property");
    a->get_property_reply = (xcb_get_property_reply_t * (*)(xcb_connection_t*, xcb_get_property_cookie_t, void*)) x11_sym(x->lib, "xcb_get_property_reply");
    a->get_property_value = (void* (*)(const xcb_get_property_reply_t*))x11_sym(x->lib, "xcb_get_property_value");
    a->get_property_value_length = (int (*)(const xcb_get_property_reply_t*))x11_sym(x->lib, "xcb_get_property_value_length");
    a->send_event = (xcb_void_cookie_t (*)(xcb_connection_t*, u8, xcb_window_t, u32, const char*))x11_sym(x->lib, "xcb_send_event");
    a->intern_atom = (xcb_intern_atom_cookie_t (*)(xcb_connection_t*, u8, u16, const char*))x11_sym(x->lib, "xcb_intern_atom");
    a->intern_atom_reply = (xcb_intern_atom_reply_t * (*)(xcb_connection_t*, xcb_intern_atom_cookie_t, void*)) x11_sym(x->lib, "xcb_intern_atom_reply");

    return a->connect && a->connection_has_error && a->disconnect && a->get_file_descriptor && a->flush && a->generate_id && a->get_setup && a->setup_roots_iterator && a->poll_for_event && a->create_window && a->destroy_window &&
           a->change_property && a->set_selection_owner && a->get_selection_owner && a->get_selection_owner_reply && a->convert_selection && a->get_property && a->get_property_reply && a->get_property_value &&
           a->get_property_value_length && a->send_event && a->intern_atom && a->intern_atom_reply;
}

static xcb_atom_t x11_atom(X11_State* x, const char* name)
{
    xcb_intern_atom_cookie_t c = x->api.intern_atom(x->conn, 0, (u16)strlen(name), name);
    xcb_intern_atom_reply_t* r = x->api.intern_atom_reply(x->conn, c, NULL);
    if (!r)
        return XCB_ATOM_NONE;
    xcb_atom_t a = r->atom;
    free(r);
    return a;
}

static X11_Owned* owned_for(X11_State* x, xcb_atom_t sel)
{
    if (sel == x->clipboard)
        return &x->clip_owned;
    if (sel == x->primary)
        return &x->prim_owned;
    return NULL;
}

static xcb_atom_t sel_atom(X11_State* x, Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY ? x->primary : x->clipboard; }

static X11_Owned* owned_for_channel(X11_State* x, Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY ? &x->prim_owned : &x->clip_owned; }

static void serve_request(X11_State* x, xcb_selection_request_event_t* req)
{
    X11_Owned*                   own = owned_for(x, req->selection);
    xcb_selection_notify_event_t notify;
    memset(&notify, 0, sizeof notify);
    notify.response_type = XCB_SELECTION_NOTIFY;
    notify.time = req->time;
    notify.requestor = req->requestor;
    notify.selection = req->selection;
    notify.target = req->target;
    notify.property = req->property;

    if (!own || !own->valid)
        notify.property = XCB_ATOM_NONE;
    else if (req->target == x->targets)
    {
        xcb_atom_t types[2] = { x->targets, x->utf8_string };
        x->api.change_property(x->conn, XCB_PROP_MODE_REPLACE, req->requestor, req->property, XCB_ATOM_ATOM, 32, 2, types);
    }
    else if (req->target == x->utf8_string || req->target == XCB_ATOM_STRING)
        x->api.change_property(x->conn, XCB_PROP_MODE_REPLACE, req->requestor, req->property, x->utf8_string, 8, (u32)own->bytes.len, own->bytes.data);
    else
        notify.property = XCB_ATOM_NONE;

    x->api.send_event(x->conn, 0, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char*)&notify);
    x->api.flush(x->conn);
}

static void handle_event(X11_State* x, xcb_generic_event_t* ev)
{
    u8 type = ev->response_type & 0x7f;
    if (type == XCB_SELECTION_REQUEST)
        serve_request(x, (xcb_selection_request_event_t*)ev);
    else if (type == XCB_SELECTION_CLEAR)
    {
        xcb_selection_clear_event_t* c = (xcb_selection_clear_event_t*)ev;
        X11_Owned*                   own = owned_for(x, c->selection);
        if (own && own->valid)
        {
            if (own->bytes.data)
                mel_dealloc(mel_clip__alloc(), own->bytes.data);
            own->bytes = STR8_EMPTY;
            own->valid = false;
        }
    }
}

static bool x11_source_prepare(Mel_Reactor_Source* source, i32* timeout)
{
    (void)source;
    *timeout = MEL_REACTOR_FOREVER;
    g_x.api.flush(g_x.conn);
    return false;
}

static bool x11_source_check(Mel_Reactor_Source* source)
{
    if (source->poll_count == 0 || !source->polls[0])
        return false;
    return (source->polls[0]->revents & (MEL_REACTOR_POLL_IN | MEL_REACTOR_POLL_HUP | MEL_REACTOR_POLL_ERR)) != 0;
}

static bool x11_source_dispatch(Mel_Reactor_Source* source, Mel_Reactor_Source_Proc cb, void* user)
{
    (void)source;
    (void)cb;
    (void)user;
    for (xcb_generic_event_t* ev; (ev = g_x.api.poll_for_event(g_x.conn));)
    {
        handle_event(&g_x, ev);
        free(ev);
    }
    if (g_x.api.connection_has_error(g_x.conn))
    {
        mel_log_error("clipboard", "x11 backend: X connection lost");
        return false;
    }
    g_x.api.flush(g_x.conn);
    return true;
}

static const Mel_Reactor_Source_Callbacks g_x11_cb = {
    .prepare = x11_source_prepare,
    .check = x11_source_check,
    .dispatch = x11_source_dispatch,
    .finalize = NULL,
};

bool mel_clip__x11_init(void)
{
    X11_State* x = &g_x;
    if (x->ok)
        return true;
    if (!x11_load(x))
    {
        if (x->lib)
        {
            dlclose(x->lib);
            x->lib = NULL;
        }
        return false;
    }

    int scr = 0;
    x->conn = x->api.connect(NULL, &scr);
    if (!x->conn || x->api.connection_has_error(x->conn))
    {
        if (x->conn)
            x->api.disconnect(x->conn);
        dlclose(x->lib);
        memset(x, 0, sizeof *x);
        return false;
    }

    xcb_screen_iterator_t it = x->api.setup_roots_iterator(x->api.get_setup(x->conn));
    if (!it.rem || !it.data)
    {
        x->api.disconnect(x->conn);
        dlclose(x->lib);
        memset(x, 0, sizeof *x);
        return false;
    }
    x->root = it.data->root;
    xcb_visualid_t visual = it.data->root_visual;
    u8             depth = it.data->root_depth;

    x->window = x->api.generate_id(x->conn);
    u32 mask = XCB_CW_EVENT_MASK;
    u32 values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
    x->api.create_window(x->conn, depth, x->window, x->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual, mask, values);

    x->clipboard = x11_atom(x, "CLIPBOARD");
    x->primary = XCB_ATOM_PRIMARY;
    x->targets = x11_atom(x, "TARGETS");
    x->utf8_string = x11_atom(x, "UTF8_STRING");
    x->prop = x11_atom(x, "MEL_CLIP_PROP");

    Mel_Reactor* reactor = mel_clip__reactor();
    if (reactor)
    {
        x->source = mel_reactor_source_new(&g_x11_cb, sizeof(Mel_Reactor_Source));
        x->poll = (Mel_Reactor_Poll){ .handle = x->api.get_file_descriptor(x->conn), .events = MEL_REACTOR_POLL_IN };
        mel_reactor_source_add_poll(x->source, &x->poll);
        mel_reactor_source_set_priority(x->source, MEL_REACTOR_PRIORITY_HIGH);
        mel_reactor_source_attach(reactor, x->source);
    }
    x->api.flush(x->conn);
    x->ok = true;
    return true;
}

void mel_clip__x11_shutdown(void)
{
    X11_State* x = &g_x;
    if (!x->ok)
        return;
    if (x->source)
    {
        mel_reactor_source_destroy(x->source);
        x->source = NULL;
    }
    const Mel_Alloc* al = mel_clip__alloc();
    if (x->clip_owned.bytes.data && al)
        mel_dealloc(al, x->clip_owned.bytes.data);
    if (x->prim_owned.bytes.data && al)
        mel_dealloc(al, x->prim_owned.bytes.data);
    if (x->window)
        x->api.destroy_window(x->conn, x->window);
    if (x->conn)
        x->api.disconnect(x->conn);
    if (x->lib)
        dlclose(x->lib);
    memset(x, 0, sizeof *x);
}

bool mel_clip__x11_owns(Mel_Clip_Channel ch) { return owned_for_channel(&g_x, ch)->valid; }

u64 mel_clip__x11_sequence(Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY ? g_x.prim_seq : g_x.clip_seq; }

static i64 now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000000000ll + (i64)ts.tv_nsec;
}

static bool wait_selection_notify(X11_State* x, xcb_selection_notify_event_t* out)
{
    i64 deadline = now_ns() + 1000000000ll;
    while (now_ns() < deadline)
    {
        for (xcb_generic_event_t* ev; (ev = x->api.poll_for_event(x->conn));)
        {
            u8 type = ev->response_type & 0x7f;
            if (type == XCB_SELECTION_NOTIFY)
            {
                *out = *(xcb_selection_notify_event_t*)ev;
                free(ev);
                return true;
            }
            handle_event(x, ev);
            free(ev);
        }
        if (x->api.connection_has_error(x->conn))
            return false;
        struct timespec nap = { 0, 1000000 };
        nanosleep(&nap, NULL);
    }
    return false;
}

static str8 read_property_bytes(X11_State* x, const Mel_Alloc* al, u32* out_len)
{
    xcb_get_property_cookie_t c = x->api.get_property(x->conn, 0, x->window, x->prop, 0, 0, 0x1fffffff);
    xcb_get_property_reply_t* r = x->api.get_property_reply(x->conn, c, NULL);
    if (!r)
        return STR8_EMPTY;
    int   n = x->api.get_property_value_length(r);
    void* p = x->api.get_property_value(r);
    str8  out = STR8_EMPTY;
    if (n > 0 && p && al)
    {
        u8* d = (u8*)mel_alloc(al, (usize)n);
        if (d)
        {
            memcpy(d, p, (usize)n);
            out = (str8){ d, (size)n };
            *out_len = (u32)n;
        }
    }
    free(r);
    return out;
}

void mel_clip__x11_read(Mel_Clip_Job* job)
{
    X11_State*       x = &g_x;
    Mel_Clip_Channel ch = mel_clip_job_channel(job);
    xcb_atom_t       sel = sel_atom(x, ch);

    if (mel_clip__x11_owns(ch))
    {
        X11_Owned* own = owned_for_channel(x, ch);
        if (own->valid && own->bytes.len > 0)
            mel_clip_job_emit(job, MEL_CLIP_FMT_TEXT, own->bytes.data, (usize)own->bytes.len);
        mel_clip_job_resolve(job, own->valid && own->bytes.len > 0 ? MEL_CLIP_OK : MEL_CLIP_RESULT_EMPTY);
        return;
    }

    xcb_get_selection_owner_cookie_t oc = x->api.get_selection_owner(x->conn, sel);
    xcb_get_selection_owner_reply_t* orp = x->api.get_selection_owner_reply(x->conn, oc, NULL);
    xcb_window_t                     owner = orp ? orp->owner : XCB_NONE;
    if (orp)
        free(orp);
    if (owner == XCB_NONE)
    {
        mel_clip_job_resolve(job, MEL_CLIP_RESULT_EMPTY);
        return;
    }

    x->api.convert_selection(x->conn, x->window, sel, x->utf8_string, x->prop, XCB_CURRENT_TIME);
    x->api.flush(x->conn);

    xcb_selection_notify_event_t notify;
    if (!wait_selection_notify(x, &notify) || notify.property == XCB_ATOM_NONE)
    {
        mel_clip_job_resolve(job, MEL_CLIP_RESULT_EMPTY);
        return;
    }
    const Mel_Alloc* al = mel_clip_job_alloc(job);
    u32              n = 0;
    str8             bytes = read_property_bytes(x, al, &n);
    if (n > 0 && bytes.data)
    {
        mel_clip_job_emit(job, MEL_CLIP_FMT_TEXT, bytes.data, (usize)n);
        mel_dealloc(al, bytes.data);
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
    else
        mel_clip_job_resolve(job, MEL_CLIP_RESULT_EMPTY);
}

void mel_clip__x11_write(Mel_Clip_Job* job)
{
    X11_State*       x = &g_x;
    Mel_Clip_Channel ch = mel_clip_job_channel(job);
    xcb_atom_t       sel = sel_atom(x, ch);
    X11_Owned*       own = owned_for_channel(x, ch);

    str8 text = STR8_EMPTY;
    u32  reps = mel_clip_job_rep_count(job, 0);
    for (u32 r = 0; r < reps; r++)
    {
        Mel_Clip_Rep rep = mel_clip_job_rep(job, 0, r);
        if (rep.format == MEL_CLIP_FMT_TEXT)
            text = rep.bytes;
        else
            mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
    }
    if (mel_clip_job_item_count(job) > 1)
        mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);

    const Mel_Alloc* al = mel_clip__alloc();
    if (own->bytes.data && al)
        mel_dealloc(al, own->bytes.data);
    own->bytes = STR8_EMPTY;
    if (text.len > 0 && al)
    {
        u8* d = (u8*)mel_alloc(al, (usize)text.len);
        if (d)
        {
            memcpy(d, text.data, (usize)text.len);
            own->bytes = (str8){ d, text.len };
        }
    }
    own->valid = true;

    x->api.set_selection_owner(x->conn, x->window, sel, XCB_CURRENT_TIME);
    x->api.flush(x->conn);

    if (mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY)
        x->prim_seq++;
    else
        x->clip_seq++;

    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__x11_clear(Mel_Clip_Job* job)
{
    X11_State*       x = &g_x;
    Mel_Clip_Channel ch = mel_clip_job_channel(job);
    xcb_atom_t       sel = sel_atom(x, ch);
    X11_Owned*       own = owned_for_channel(x, ch);

    const Mel_Alloc* al = mel_clip__alloc();
    if (own->bytes.data && al)
        mel_dealloc(al, own->bytes.data);
    own->bytes = STR8_EMPTY;
    own->valid = false;

    x->api.set_selection_owner(x->conn, XCB_NONE, sel, XCB_CURRENT_TIME);
    x->api.flush(x->conn);
    if (mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY)
        x->prim_seq++;
    else
        x->clip_seq++;
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__x11_query(Mel_Clip_Job* job)
{
    X11_State*       x = &g_x;
    Mel_Clip_Channel ch = mel_clip_job_channel(job);
    xcb_atom_t       sel = sel_atom(x, ch);

    xcb_get_selection_owner_cookie_t oc = x->api.get_selection_owner(x->conn, sel);
    xcb_get_selection_owner_reply_t* orp = x->api.get_selection_owner_reply(x->conn, oc, NULL);
    xcb_window_t                     owner = orp ? orp->owner : XCB_NONE;
    if (orp)
        free(orp);
    if (owner != XCB_NONE)
        mel_clip_job_emit_format(job, MEL_CLIP_FMT_TEXT);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__x11_has(Mel_Clip_Job* job)
{
    X11_State*                       x = &g_x;
    xcb_atom_t                       sel = sel_atom(x, mel_clip_job_channel(job));
    xcb_get_selection_owner_cookie_t oc = x->api.get_selection_owner(x->conn, sel);
    xcb_get_selection_owner_reply_t* orp = x->api.get_selection_owner_reply(x->conn, oc, NULL);
    xcb_window_t                     owner = orp ? orp->owner : XCB_NONE;
    if (orp)
        free(orp);
    mel_clip_job_set_present(job, owner != XCB_NONE);
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void* mel_clip__x11_native(void) { return g_x.conn; }
