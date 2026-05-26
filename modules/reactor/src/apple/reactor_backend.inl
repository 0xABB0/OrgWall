#ifdef _CLANGD
#pragma once
#include "../reactor.c"
#endif

#include <CoreFoundation/CoreFoundation.h>
#include <poll.h>

// ATTACHED hands the reactor to an already-running CFRunLoop instead of
// spawning a thread: a re-arming timer on the current loop drives one
// iteration per fire, so UIApplicationMain's main loop owns the reactor and
// everything runs on the main thread. THREADED keeps the blocking own-loop
// path (macOS).
#define MEL_REACTOR_BACKEND_HAS_ATTACHED 1

#if MEL_PLATFORM_OSX
extern void mel_reactor__macos_drain_events(void);
#endif

static void reactor_apple_wake_perform(void* info)
{
    (void)info;
}

static void reactor_apple_arm(Mel_Reactor* r, CFAbsoluteTime when);
static void reactor_apple_disarm(Mel_Reactor* r);
static void reactor_apple_unregister_fds(Mel_Reactor* r);

static bool reactor_backend_init(Mel_Reactor* r)
{
    CFRunLoopRef loop = CFRunLoopGetCurrent();
    CFRetain(loop);

    CFRunLoopSourceContext ctx = {0};
    ctx.perform = reactor_apple_wake_perform;
    CFRunLoopSourceRef wake = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
    CFRunLoopAddSource(loop, wake, kCFRunLoopCommonModes);

    r->cf_loop = loop;
    r->cf_wake = wake;
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    if (r->cf_tick) {
        CFRunLoopTimerInvalidate((CFRunLoopTimerRef)r->cf_tick);
        CFRelease((CFRunLoopTimerRef)r->cf_tick);
        r->cf_tick = NULL;
    }
    if (r->cf_wake) {
        CFRunLoopSourceInvalidate((CFRunLoopSourceRef)r->cf_wake);
        CFRelease((CFRunLoopSourceRef)r->cf_wake);
    }
    reactor_apple_unregister_fds(r);
    if (r->cf_loop) CFRelease((CFRunLoopRef)r->cf_loop);
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    // ATTACHED: (re)arm the tick to fire now, recreating it if idle had torn it
    // down. THREADED: poke the no-op source that breaks CFRunLoopRunInMode.
    if (r->mode == MEL_REACTOR_ATTACHED) {
        reactor_apple_arm(r, CFAbsoluteTimeGetCurrent());
    } else {
        CFRunLoopSourceSignal((CFRunLoopSourceRef)r->cf_wake);
    }
    CFRunLoopWakeUp((CFRunLoopRef)r->cf_loop);
}

static void reactor_apple_tick(CFRunLoopTimerRef timer, void* info)
{
    (void)timer;
    Mel_Reactor* r = info;
    if (!reactor_iterate(r, true)) reactor_attached_destroy(r);
}

// The tick exists only while a source needs a future wakeup. arm creates and
// adds it on demand; disarm removes it so an idle reactor has nothing on the
// run loop at all.
static void reactor_apple_arm(Mel_Reactor* r, CFAbsoluteTime when)
{
    if (r->cf_tick) {
        CFRunLoopTimerSetNextFireDate((CFRunLoopTimerRef)r->cf_tick, when);
        return;
    }
    CFRunLoopTimerContext ctx = {0};
    ctx.info = r;
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(kCFAllocatorDefault, when, 1.0e10,
                                               0, 0, reactor_apple_tick, &ctx);
    r->cf_tick = t;
    CFRunLoopAddTimer((CFRunLoopRef)r->cf_loop, t, kCFRunLoopCommonModes);
}

static void reactor_apple_disarm(Mel_Reactor* r)
{
    if (!r->cf_tick) return;
    CFRunLoopTimerInvalidate((CFRunLoopTimerRef)r->cf_tick);
    CFRelease((CFRunLoopTimerRef)r->cf_tick);
    r->cf_tick = NULL;
}

static void reactor_backend_attached_run(Mel_Reactor* r)
{
    // We are already on the thread whose run loop will drive us, so run init
    // inline. The first iterate then arms a wakeup only if a source needs one;
    // with nothing pending it stays detached and the loop just runs its host.
    if (r->init && !r->init(r, r->init_user)) {
        atomic_store(&r->running, false);
        reactor_attached_destroy(r);
        return;
    }
    r->init_done = true;
    if (!reactor_iterate(r, true)) reactor_attached_destroy(r);
}

// Threaded (own-loop) fd callback: runs inside CFRunLoopRunInMode and records
// readiness straight onto the poll.
static void reactor_apple_fd_callback(CFFileDescriptorRef fdref, CFOptionFlags types, void* info)
{
    (void)fdref;
    Mel_Reactor_Poll* poll = info;
    if (!poll) return;
    if (types & kCFFileDescriptorReadCallBack)  poll->revents |= MEL_REACTOR_POLL_IN;
    if (types & kCFFileDescriptorWriteCallBack) poll->revents |= MEL_REACTOR_POLL_OUT;
}

// Attached fd callback: the host loop owns the wait, so an fd going ready only
// needs to wake an iteration — the readiness itself is captured by the poll()
// inside reactor_backend_wait_attached. One-shot is fine: each iterate tears the
// sources down and rebuilds them.
static void reactor_apple_fd_wake(CFFileDescriptorRef fdref, CFOptionFlags types, void* info)
{
    (void)fdref; (void)types;
    reactor_apple_arm((Mel_Reactor*)info, CFAbsoluteTimeGetCurrent());
}

static void reactor_apple_unregister_fds(Mel_Reactor* r)
{
    for (usize i = 0; i < r->cf_fd_count; i++) {
        CFRunLoopRemoveSource((CFRunLoopRef)r->cf_loop, (CFRunLoopSourceRef)r->cf_fd_srcs[i],
                              kCFRunLoopCommonModes);
        CFRelease((CFRunLoopSourceRef)r->cf_fd_srcs[i]);
        CFFileDescriptorInvalidate((CFFileDescriptorRef)r->cf_fd_refs[i]);
        CFRelease((CFFileDescriptorRef)r->cf_fd_refs[i]);
    }
    r->cf_fd_count = 0;
}

// ATTACHED never blocks. It captures current fd readiness with a single
// non-blocking poll() for this iterate, registers each fd as a CFFileDescriptor
// source so future activity wakes the host loop (no polling), and arms the tick
// only when a timer is pending. With nothing pending it leaves the run loop with
// no reactor wakeups at all.
static bool reactor_backend_wait_attached(Mel_Reactor* r, Mel_Reactor_Poll** polls,
                                          usize poll_count, i32 timeout)
{
    reactor_apple_unregister_fds(r);

    struct pollfd fds[MEL_REACTOR_MAX_POLLS];
    nfds_t        n = 0;
    for (usize i = 0; i < poll_count && n < MEL_REACTOR_MAX_POLLS; i++) {
        if (!polls[i]) continue;
        polls[i]->revents = 0;
        short events = 0;
        if (polls[i]->events & MEL_REACTOR_POLL_IN)  events |= POLLIN;
        if (polls[i]->events & MEL_REACTOR_POLL_OUT) events |= POLLOUT;
        fds[n].fd = (int)polls[i]->handle; fds[n].events = events; fds[n].revents = 0;
        n++;
    }
    if (n > 0 && poll(fds, n, 0) > 0) {
        nfds_t slot = 0;
        for (usize i = 0; i < poll_count; i++) {
            if (!polls[i]) continue;
            if (slot >= n) break;
            short re = fds[slot].revents; u32 out = 0;
            if (re & POLLIN)  out |= MEL_REACTOR_POLL_IN;
            if (re & POLLOUT) out |= MEL_REACTOR_POLL_OUT;
            if (re & POLLERR) out |= MEL_REACTOR_POLL_ERR;
            if (re & POLLHUP) out |= MEL_REACTOR_POLL_HUP;
            polls[i]->revents = out;
            slot++;
        }
    }

    for (usize i = 0; i < poll_count && r->cf_fd_count < MEL_REACTOR_MAX_POLLS; i++) {
        if (!polls[i]) continue;
        CFFileDescriptorContext ctx = {0};
        ctx.info = r;
        CFFileDescriptorRef fdref = CFFileDescriptorCreate(
            kCFAllocatorDefault, (CFFileDescriptorNativeDescriptor)polls[i]->handle,
            false, reactor_apple_fd_wake, &ctx);
        if (!fdref) continue;
        CFOptionFlags want = 0;
        if (polls[i]->events & MEL_REACTOR_POLL_IN)  want |= kCFFileDescriptorReadCallBack;
        if (polls[i]->events & MEL_REACTOR_POLL_OUT) want |= kCFFileDescriptorWriteCallBack;
        CFFileDescriptorEnableCallBacks(fdref, want);
        CFRunLoopSourceRef src = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0);
        CFRunLoopAddSource((CFRunLoopRef)r->cf_loop, src, kCFRunLoopCommonModes);
        r->cf_fd_refs[r->cf_fd_count] = fdref;
        r->cf_fd_srcs[r->cf_fd_count] = src;
        r->cf_fd_count++;
    }

    if (timeout >= 0) reactor_apple_arm(r, CFAbsoluteTimeGetCurrent() + (CFTimeInterval)timeout / 1000.0);
    else              reactor_apple_disarm(r);
    return true;
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    if (r->mode == MEL_REACTOR_ATTACHED)
        return reactor_backend_wait_attached(r, polls, poll_count, timeout);

    CFRunLoopRef        loop = (CFRunLoopRef)r->cf_loop;
    CFFileDescriptorRef fdrefs[MEL_REACTOR_MAX_POLLS];
    CFRunLoopSourceRef  srcs[MEL_REACTOR_MAX_POLLS];
    usize               n = 0;

    for (usize i = 0; i < poll_count && n < MEL_REACTOR_MAX_POLLS; i++) {
        if (!polls[i]) continue;
        polls[i]->revents = 0;

        CFFileDescriptorContext ctx = {0};
        ctx.info = polls[i];
        CFFileDescriptorRef fdref = CFFileDescriptorCreate(
            kCFAllocatorDefault, (CFFileDescriptorNativeDescriptor)polls[i]->handle,
            false, reactor_apple_fd_callback, &ctx);
        if (!fdref) continue;

        CFOptionFlags want = 0;
        if (polls[i]->events & MEL_REACTOR_POLL_IN)  want |= kCFFileDescriptorReadCallBack;
        if (polls[i]->events & MEL_REACTOR_POLL_OUT) want |= kCFFileDescriptorWriteCallBack;
        CFFileDescriptorEnableCallBacks(fdref, want);

        CFRunLoopSourceRef src = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0);
        CFRunLoopAddSource(loop, src, kCFRunLoopDefaultMode);

        fdrefs[n] = fdref;
        srcs[n]   = src;
        n++;
    }

    CFTimeInterval secs;
    if      (timeout < 0)  secs = 1.0e9;
    else if (timeout == 0) secs = 0.0;
    else                   secs = (CFTimeInterval)timeout / 1000.0;

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, true);

#if MEL_PLATFORM_OSX
    mel_reactor__macos_drain_events();
#endif

    for (usize i = 0; i < n; i++) {
        CFRunLoopRemoveSource(loop, srcs[i], kCFRunLoopDefaultMode);
        CFRelease(srcs[i]);
        CFFileDescriptorInvalidate(fdrefs[i]);
        CFRelease(fdrefs[i]);
    }
    return true;
}
