#include <vat/vat.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <collection/list.h>
#include <debug/assert.h>

#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#include <stdatomic.h>

typedef id (*msg_t)(id, SEL);
typedef void (*msg_void_t)(id, SEL);
typedef void (*msg_void_id_t)(id, SEL, id);
typedef void (*msg_void_id_bool_t)(id, SEL, id, BOOL);
typedef id (*msg_date_t)(id, SEL, double);
typedef id (*msg_nextevent_t)(id, SEL, unsigned long long, id, id, BOOL);
typedef id (*msg_event_t)(id, SEL, unsigned long long, struct CGPoint, unsigned long long, double, long, id, short, long, long);

#define NS_ANY_EVENT_MASK         0xffffffffffffffffULL
#define NS_EVENT_TYPE_APP_DEFINED 15

static id objc_class(const char* name) { return (id)objc_getClass(name); }

static id msend(id obj, const char* sel) { return ((msg_t)objc_msgSend)(obj, sel_registerName(sel)); }

typedef struct Cocoa_Waiter Cocoa_Waiter;

typedef struct
{
    Mel_Vat_Wakeable*   wakeable;
    Cocoa_Waiter*       waiter;
    CFFileDescriptorRef fdref;
    CFRunLoopSourceRef  source;
} Cocoa_Fd_Bridge;

struct Cocoa_Waiter
{
    Mel_Vat_Waiter   base;
    const Mel_Alloc* alloc;
    id               app;
    id               mode;
    CFRunLoopRef     runloop;
    bool             launched;
    atomic_bool      rung;
    Mel_Array(Cocoa_Fd_Bridge*) bridges;
};

static void cocoa_ring(Mel_Vat_Waiter* waiter);

static CFOptionFlags cocoa_fd_flags(u32 events)
{
    CFOptionFlags flags = 0;
    if (events & MEL_VAT_WAKE_IN)
        flags |= kCFFileDescriptorReadCallBack;
    if (events & MEL_VAT_WAKE_OUT)
        flags |= kCFFileDescriptorWriteCallBack;
    return flags;
}

static void cocoa_fd_fired(CFFileDescriptorRef fdref, CFOptionFlags types, void* info)
{
    MEL_UNUSED(fdref);
    Cocoa_Fd_Bridge* bridge = info;
    if (types & kCFFileDescriptorReadCallBack)
        bridge->wakeable->revents |= MEL_VAT_WAKE_IN;
    if (types & kCFFileDescriptorWriteCallBack)
        bridge->wakeable->revents |= MEL_VAT_WAKE_OUT;
    cocoa_ring(&bridge->waiter->base);
}

static bool cocoa_arm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Cocoa_Waiter* w = mel_container_of(waiter, Cocoa_Waiter, base);
    mel_assert_msg("cocoa waiter: arm off the opening thread", CFRunLoopGetCurrent() == w->runloop);
    for (usize i = 0; i < w->bridges.count; i++)
    {
        if (w->bridges.items[i]->wakeable == wakeable)
        {
            CFFileDescriptorEnableCallBacks(w->bridges.items[i]->fdref, cocoa_fd_flags(wakeable->events));
            return true;
        }
    }
    Cocoa_Fd_Bridge* bridge = mel_alloc_type(w->alloc, Cocoa_Fd_Bridge);
    bridge->wakeable = wakeable;
    bridge->waiter = w;
    CFFileDescriptorContext ctx = { 0, bridge, NULL, NULL, NULL };
    bridge->fdref = CFFileDescriptorCreate(kCFAllocatorDefault, (int)wakeable->handle, false, cocoa_fd_fired, &ctx);
    if (bridge->fdref == NULL)
    {
        mel_dealloc(w->alloc, bridge);
        return false;
    }
    bridge->source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, bridge->fdref, 0);
    CFRunLoopAddSource(w->runloop, bridge->source, kCFRunLoopCommonModes);
    CFFileDescriptorEnableCallBacks(bridge->fdref, cocoa_fd_flags(wakeable->events));
    mel_array_push(&w->bridges, bridge);
    return true;
}

static void cocoa_disarm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Cocoa_Waiter* w = mel_container_of(waiter, Cocoa_Waiter, base);
    mel_assert_msg("cocoa waiter: disarm off the opening thread", CFRunLoopGetCurrent() == w->runloop);
    for (usize i = 0; i < w->bridges.count; i++)
    {
        Cocoa_Fd_Bridge* bridge = w->bridges.items[i];
        if (bridge->wakeable != wakeable)
            continue;
        CFRunLoopRemoveSource(w->runloop, bridge->source, kCFRunLoopCommonModes);
        CFFileDescriptorInvalidate(bridge->fdref);
        CFRelease(bridge->source);
        CFRelease(bridge->fdref);
        mel_array_remove_unordered(&w->bridges, i);
        mel_dealloc(w->alloc, bridge);
        return;
    }
}

static i32 cocoa_pump_one(Cocoa_Waiter* w, id until)
{
    SEL next = sel_registerName("nextEventMatchingMask:untilDate:inMode:dequeue:");
    id  event = ((msg_nextevent_t)objc_msgSend)(w->app, next, NS_ANY_EVENT_MASK, until, w->mode, YES);
    if (event == NULL)
        return 0;
    ((msg_void_id_t)objc_msgSend)(w->app, sel_registerName("sendEvent:"), event);
    return 1;
}

static i32 cocoa_wait(Mel_Vat_Waiter* waiter, i64 timeout_ns)
{
    Cocoa_Waiter* w = mel_container_of(waiter, Cocoa_Waiter, base);
    atomic_store_explicit(&w->rung, false, memory_order_seq_cst);
    for (usize i = 0; i < w->bridges.count; i++)
        CFFileDescriptorEnableCallBacks(w->bridges.items[i]->fdref, cocoa_fd_flags(w->bridges.items[i]->wakeable->events));
    if (!w->launched)
    {
        ((msg_void_t)objc_msgSend)(w->app, sel_registerName("finishLaunching"));
        w->launched = true;
    }
    if (timeout_ns == 0)
    {
        id  past = msend(objc_class("NSDate"), "distantPast");
        i32 pumped = 0;
        while (cocoa_pump_one(w, past) == 1)
            pumped++;
        return pumped;
    }
    id until = timeout_ns < 0 ? msend(objc_class("NSDate"), "distantFuture") : ((msg_date_t)objc_msgSend)(objc_class("NSDate"), sel_registerName("dateWithTimeIntervalSinceNow:"), (double)timeout_ns / 1e9);
    return cocoa_pump_one(w, until);
}

static void cocoa_ring(Mel_Vat_Waiter* waiter)
{
    Cocoa_Waiter* w = mel_container_of(waiter, Cocoa_Waiter, base);
    if (atomic_exchange_explicit(&w->rung, true, memory_order_seq_cst))
        return;
    id event = ((msg_event_t)objc_msgSend)(objc_class("NSEvent"),
                                           sel_registerName("otherEventWithType:location:modifierFlags:timestamp:"
                                                            "windowNumber:context:subtype:data1:data2:"),
                                           NS_EVENT_TYPE_APP_DEFINED,
                                           (struct CGPoint){ 0, 0 },
                                           0,
                                           0.0,
                                           0,
                                           (id)0,
                                           0,
                                           0,
                                           0);
    ((msg_void_id_bool_t)objc_msgSend)(w->app, sel_registerName("postEvent:atStart:"), event, YES);
}

static void cocoa_close(Mel_Vat_Waiter* waiter)
{
    Cocoa_Waiter* w = mel_container_of(waiter, Cocoa_Waiter, base);
    while (w->bridges.count > 0)
        cocoa_disarm(waiter, w->bridges.items[0]->wakeable);
    mel_array_free(&w->bridges);
    mel_dealloc(w->alloc, w);
}

static const Mel_Vat_Waiter_Vtbl cocoa_vtbl = { cocoa_arm, cocoa_disarm, cocoa_wait, cocoa_ring, cocoa_close };

Mel_Vat_Waiter* mel_vat_waiter_cocoa(const Mel_Alloc* alloc)
{
    mel_assert(alloc != NULL);
    Cocoa_Waiter* w = mel_alloc_type(alloc, Cocoa_Waiter);
    w->base.vt = &cocoa_vtbl;
    w->alloc = alloc;
    w->app = msend(objc_class("NSApplication"), "sharedApplication");
    w->mode = (id)CFSTR("kCFRunLoopDefaultMode");
    w->runloop = CFRunLoopGetCurrent();
    w->launched = false;
    atomic_init(&w->rung, false);
    mel_array_init(&w->bridges, alloc);
    return &w->base;
}

Mel_Vat_Waiter* mel_vat_waiter_ui(const Mel_Alloc* alloc) { return mel_vat_waiter_cocoa(alloc); }

Mel_Vat_Waiter* mel_vat_waiter_io(const Mel_Alloc* alloc) { return mel_vat_waiter_kqueue(alloc); }
