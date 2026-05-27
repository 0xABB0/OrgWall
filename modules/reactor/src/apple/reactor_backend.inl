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
static void reactor_apple_reconcile_fds(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count);

// Threaded uses the loop's default mode (CFRunLoopRunInMode runs that mode);
// attached registers in the common modes so the host loop fires us whatever
// mode it is parked in. The mode is fixed for a reactor's lifetime, so an fd
// source is always removed from the mode it was added to.
static CFStringRef reactor_apple_mode(const Mel_Reactor* r)
{
    return r->mode == MEL_REACTOR_ATTACHED ? kCFRunLoopCommonModes : kCFRunLoopDefaultMode;
}

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
    reactor_apple_reconcile_fds(r, NULL, 0);
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

// Threaded (own-loop) fd callback: runs inside CFRunLoopRunInMode and records
// readiness straight onto the poll. The callback is one-shot per enable, so the
// run-mode path re-enables before each wait.
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
// inside reactor_backend_wait_attached.
static void reactor_apple_fd_wake(CFFileDescriptorRef fdref, CFOptionFlags types, void* info)
{
    (void)fdref; (void)types;
    reactor_apple_arm((Mel_Reactor*)info, CFAbsoluteTimeGetCurrent());
}

// Reconcile the registered CFFileDescriptor set to exactly the desired poll set:
// drop registrations no longer desired, create the newly desired, leave stable
// ones in place. The expensive create/add and remove/release happen only on
// change, so a steady fd set costs nothing across iterations. A NULL/empty
// desired set (shutdown) tears everything down.
static void reactor_apple_reconcile_fds(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count)
{
    CFRunLoopRef loop = (CFRunLoopRef)r->cf_loop;
    CFStringRef  mode = reactor_apple_mode(r);

    for (usize i = 0; i < r->cf_fd_count;) {
        bool desired = false;
        for (usize j = 0; j < poll_count; j++) {
            if (polls[j] && polls[j] == r->cf_fd_polls[i]) { desired = true; break; }
        }
        if (desired) { i++; continue; }
        CFRunLoopRemoveSource(loop, (CFRunLoopSourceRef)r->cf_fd_srcs[i], mode);
        CFRelease((CFRunLoopSourceRef)r->cf_fd_srcs[i]);
        CFFileDescriptorInvalidate((CFFileDescriptorRef)r->cf_fd_refs[i]);
        CFRelease((CFFileDescriptorRef)r->cf_fd_refs[i]);
        usize last = --r->cf_fd_count;
        r->cf_fd_polls[i] = r->cf_fd_polls[last];
        r->cf_fd_refs[i]  = r->cf_fd_refs[last];
        r->cf_fd_srcs[i]  = r->cf_fd_srcs[last];
    }

    for (usize j = 0; j < poll_count && r->cf_fd_count < MEL_REACTOR_MAX_POLLS; j++) {
        if (!polls[j]) continue;
        bool registered = false;
        for (usize i = 0; i < r->cf_fd_count; i++) {
            if (r->cf_fd_polls[i] == polls[j]) { registered = true; break; }
        }
        if (registered) continue;

        CFFileDescriptorContext ctx = {0};
        ctx.info = r->mode == MEL_REACTOR_ATTACHED ? (void*)r : (void*)polls[j];
        CFFileDescriptorCallBack cb = r->mode == MEL_REACTOR_ATTACHED
            ? reactor_apple_fd_wake : reactor_apple_fd_callback;
        CFFileDescriptorRef fdref = CFFileDescriptorCreate(
            kCFAllocatorDefault, (CFFileDescriptorNativeDescriptor)polls[j]->handle, false, cb, &ctx);
        if (!fdref) continue;
        CFRunLoopSourceRef src = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0);
        CFRunLoopAddSource(loop, src, mode);
        r->cf_fd_polls[r->cf_fd_count] = polls[j];
        r->cf_fd_refs[r->cf_fd_count]  = fdref;
        r->cf_fd_srcs[r->cf_fd_count]  = src;
        r->cf_fd_count++;
    }
}

// CFFileDescriptor callbacks disable themselves once they fire, so re-arm every
// registered fd for the events its poll currently wants before each wait.
static void reactor_apple_enable_fds(Mel_Reactor* r)
{
    for (usize i = 0; i < r->cf_fd_count; i++) {
        Mel_Reactor_Poll* p = r->cf_fd_polls[i];
        CFOptionFlags want = 0;
        if (p->events & MEL_REACTOR_POLL_IN)  want |= kCFFileDescriptorReadCallBack;
        if (p->events & MEL_REACTOR_POLL_OUT) want |= kCFFileDescriptorWriteCallBack;
        CFFileDescriptorEnableCallBacks((CFFileDescriptorRef)r->cf_fd_refs[i], want);
    }
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

// ATTACHED never blocks. It captures current fd readiness with a single
// non-blocking poll() for this iterate (the persistent CFFileDescriptor sources
// only wake future iterates), and arms the tick only when a timer is pending.
// With nothing pending it leaves the run loop with no reactor wakeups at all.
static bool reactor_backend_wait_attached(Mel_Reactor* r, Mel_Reactor_Poll** polls,
                                          usize poll_count, i32 timeout)
{
    reactor_apple_reconcile_fds(r, polls, poll_count);
    reactor_apple_enable_fds(r);

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

    if (timeout >= 0) reactor_apple_arm(r, CFAbsoluteTimeGetCurrent() + (CFTimeInterval)timeout / 1000.0);
    else              reactor_apple_disarm(r);
    return true;
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    if (r->mode == MEL_REACTOR_ATTACHED)
        return reactor_backend_wait_attached(r, polls, poll_count, timeout);

    // THREADED owns the loop and blocks in it. Persistent CFFileDescriptor
    // sources both break the wait when an fd goes ready and record readiness
    // onto the poll, so no extra poll() syscall is needed.
    reactor_apple_reconcile_fds(r, polls, poll_count);
    for (usize i = 0; i < poll_count; i++) {
        if (polls[i]) polls[i]->revents = 0;
    }
    reactor_apple_enable_fds(r);

    CFTimeInterval secs;
    if      (timeout < 0)  secs = 1.0e9;
    else if (timeout == 0) secs = 0.0;
    else                   secs = (CFTimeInterval)timeout / 1000.0;

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, true);

#if MEL_PLATFORM_OSX
    mel_reactor__macos_drain_events();
#endif
    return true;
}
