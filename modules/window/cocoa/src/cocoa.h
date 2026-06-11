#pragma once

#include "../../src/window_internal.h"

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>

@interface                   MelWindowContentView: NSView
@property(assign) Mel_Window window_handle;
@end

@interface                   MelWindowObserver: NSObject
@property(assign) Mel_Window window_handle;
@end

@interface                   MelWindowDelegate: NSObject <NSWindowDelegate>
@property(assign) Mel_Window window_handle;
@end
#endif
