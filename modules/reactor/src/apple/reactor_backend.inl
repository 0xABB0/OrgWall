#ifdef _CLANGD
#pragma once
#include "../reactor.c"
#endif

#include <CoreFoundation/CoreFoundation.h>

static void reactor_apple_wake_perform(void* info)
{
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
    if (r->cf_wake) {
        CFRunLoopSourceInvalidate((CFRunLoopSourceRef)r->cf_wake);
        CFRelease((CFRunLoopSourceRef)r->cf_wake);
    }
    if (r->cf_loop) CFRelease((CFRunLoopRef)r->cf_loop);
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    CFRunLoopSourceSignal((CFRunLoopSourceRef)r->cf_wake);
    CFRunLoopWakeUp((CFRunLoopRef)r->cf_loop);
}

static void reactor_apple_fd_callback(CFFileDescriptorRef fdref, CFOptionFlags types, void* info)
{
    Mel_Reactor_Poll* poll = info;
    if (!poll) return;
    if (types & kCFFileDescriptorReadCallBack)  poll->revents |= MEL_REACTOR_POLL_IN;
    if (types & kCFFileDescriptorWriteCallBack) poll->revents |= MEL_REACTOR_POLL_OUT;
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
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

    for (usize i = 0; i < n; i++) {
        CFRunLoopRemoveSource(loop, srcs[i], kCFRunLoopDefaultMode);
        CFRelease(srcs[i]);
        CFFileDescriptorInvalidate(fdrefs[i]);
        CFRelease(fdrefs[i]);
    }
    return true;
}
