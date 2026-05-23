#import <AppKit/AppKit.h>

void mel_reactor__macos_drain_events(void)
{
    if (!NSApp) return;
    @autoreleasepool {
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (!event) break;
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
    }
}
