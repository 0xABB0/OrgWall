#include "cocoa.h"

#import <objc/runtime.h>

@implementation MelWindowContentView

- (BOOL)isFlipped
{
    return YES;
}
- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (BOOL)canBecomeKeyView
{
    return YES;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    for (NSTrackingArea* ta in self.trackingAreas)
        [self removeTrackingArea:ta];
    NSTrackingAreaOptions opts = NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect;
    NSTrackingArea*       ta = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:opts owner:self userInfo:nil];
    [self addTrackingArea:ta];
}

- (NSPoint)pt:(NSEvent*)e
{
    return [self convertPoint:e.locationInWindow fromView:nil];
}

- (void)mouseDown:(NSEvent*)e
{
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n || !n->input.on_pointer_down)
        return;
    NSPoint p = [self pt:e];
    n->input.on_pointer_down(self.window_handle, (i32)p.x, (i32)p.y, n->user);
}

- (void)mouseUp:(NSEvent*)e
{
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n || !n->input.on_pointer_up)
        return;
    NSPoint p = [self pt:e];
    n->input.on_pointer_up(self.window_handle, (i32)p.x, (i32)p.y, n->user);
}

- (void)mouseDragged:(NSEvent*)e
{
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n || !n->input.on_pointer_move)
        return;
    NSPoint p = [self pt:e];
    n->input.on_pointer_move(self.window_handle, (i32)p.x, (i32)p.y, n->user);
}

- (void)mouseMoved:(NSEvent*)e
{
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n || !n->input.on_pointer_move)
        return;
    NSPoint p = [self pt:e];
    n->input.on_pointer_move(self.window_handle, (i32)p.x, (i32)p.y, n->user);
}

- (void)keyDown:(NSEvent*)e
{
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (n && n->input.on_key_down)
        n->input.on_key_down(self.window_handle, (u32)e.keyCode, n->user);
}

- (void)keyUp:(NSEvent*)e
{
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (n && n->input.on_key_up)
        n->input.on_key_up(self.window_handle, (u32)e.keyCode, n->user);
}

@end

static void mel_window__sync(NSWindow* window, Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;
    NSSize pts = window.contentView.bounds.size;
    f32    scale = (f32)window.backingScaleFactor;
    n->point_w = (i32)pts.width;
    n->point_h = (i32)pts.height;
    n->scale = scale;
    mel_window__resized(w, (i32)(pts.width * scale), (i32)(pts.height * scale));
}

@implementation MelWindowObserver

- (void)onResize:(NSNotification*)note
{
    mel_window__sync((NSWindow*)note.object, self.window_handle);
}

- (void)onMove:(NSNotification*)note
{
    NSWindow*        window = (NSWindow*)note.object;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n)
        return;
    NSRect    fr = window.frame;
    NSScreen* screen = window.screen ?: [NSScreen mainScreen];
    CGFloat   sh = screen.frame.size.height;
    i32       x = (i32)fr.origin.x;
    i32       y = (i32)(sh - (fr.origin.y + fr.size.height));
    n->x = x;
    n->y = y;
    if (n->lifecycle.on_move)
        n->lifecycle.on_move(self.window_handle, x, y, n->user);
}

- (void)onBacking:(NSNotification*)note
{
    NSWindow*        window = (NSWindow*)note.object;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n)
        return;
    f32 scale = (f32)window.backingScaleFactor;
    if (n->display.on_scale_changed)
        n->display.on_scale_changed(self.window_handle, scale, n->user);
    if (n->display.on_hdr_changed)
        n->display.on_hdr_changed(self.window_handle, n->user);
    mel_window__sync(window, self.window_handle);
}

- (void)onScreen:(NSNotification*)note
{
    (void)note;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n)
        return;
    if (n->display.on_display_migrated)
        n->display.on_display_migrated(self.window_handle, n->user);
    if (n->display.on_hdr_changed)
        n->display.on_hdr_changed(self.window_handle, n->user);
}

- (void)onOcclusion:(NSNotification*)note
{
    NSWindow*        window = (NSWindow*)note.object;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (!n || !n->app.on_occluded)
        return;
    bool visible = (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
    n->app.on_occluded(self.window_handle, !visible, n->user);
}

- (void)onBecomeKey:(NSNotification*)note
{
    (void)note;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (n && n->lifecycle.on_focus_in)
        n->lifecycle.on_focus_in(self.window_handle, n->user);
}

- (void)onResignKey:(NSNotification*)note
{
    (void)note;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (n && n->lifecycle.on_focus_out)
        n->lifecycle.on_focus_out(self.window_handle, n->user);
}

- (void)onWillClose:(NSNotification*)note
{
    (void)note;
    Mel_Window w = self.window_handle;
    if (mel_window_is_none(w))
        return;

    [[NSNotificationCenter defaultCenter] removeObserver:self];

    Mel_Window_Node* n = mel_window__node(w);
    void*            native = n ? n->native : NULL;
    if (n)
    {
        n->native = NULL;
        n->content = NULL;
    }

    mel_window__closed(w);

    if (native)
    {
        dispatch_async(dispatch_get_main_queue(), ^{
            CFBridgingRelease(native);
        });
    }
}

- (void)onAppActive:(NSNotification*)note
{
    (void)note;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (n && n->app.on_foreground)
        n->app.on_foreground(self.window_handle, n->user);
}

- (void)onAppResign:(NSNotification*)note
{
    (void)note;
    Mel_Window_Node* n = mel_window__node(self.window_handle);
    if (n && n->app.on_background)
        n->app.on_background(self.window_handle, n->user);
}

@end

@implementation MelWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    return mel_window_should_close(self.window_handle) ? YES : NO;
}

@end

bool mel_window_should_close(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (n && n->lifecycle.on_close_request)
        return n->lifecycle.on_close_request(w, n->user);
    return true;
}

static NSString* mel_window__nsstring(str8 s)
{
    if (s.len <= 0 || s.data == NULL)
        return @"";
    return [[NSString alloc] initWithBytes:s.data length:(NSUInteger)s.len encoding:NSUTF8StringEncoding] ?: @"";
}

static void mel_window__install_default_menu(void)
{
    NSMenu*     bar = [[NSMenu alloc] init];
    NSMenuItem* app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    [NSApp setMainMenu:bar];

    NSMenu* app_menu = [[NSMenu alloc] init];
    [app_menu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    [app_item setSubmenu:app_menu];
}

bool mel_window__backend_init(void)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        mel_window__install_default_menu();
        [NSApp finishLaunching];
    }
    return true;
}

void mel_window__backend_create(Mel_Window_Node* n, const Mel_Window_Opt* o)
{
    @autoreleasepool
    {
        NSRect content = NSMakeRect(0, 0, n->w, n->h);

        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable;
        if (!o->not_closable)
            style |= NSWindowStyleMaskClosable;
        if (!o->not_resizable)
            style |= NSWindowStyleMaskResizable;
        if (o->undecorated)
            style = NSWindowStyleMaskBorderless;

        NSWindow* window = [[NSWindow alloc] initWithContentRect:content styleMask:style backing:NSBackingStoreBuffered defer:NO];

        if (o->content_native)
        {
            NSView* borrowed = (__bridge NSView*)o->content_native;
            [borrowed setFrame:content];
            [window setContentView:borrowed];
            n->content = o->content_native;
            n->borrowed = true;
        }
        else
        {
            MelWindowContentView* root = [[MelWindowContentView alloc] initWithFrame:content];
            root.window_handle = n->self;
            [window setContentView:root];
            [window makeFirstResponder:root];
            n->content = (__bridge void*)root;
        }

        MelWindowObserver* obs = [[MelWindowObserver alloc] init];
        obs.window_handle = n->self;

        NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:obs selector:@selector(onResize:) name:NSWindowDidResizeNotification object:window];
        [nc addObserver:obs selector:@selector(onMove:) name:NSWindowDidMoveNotification object:window];
        [nc addObserver:obs selector:@selector(onBacking:) name:NSWindowDidChangeBackingPropertiesNotification object:window];
        [nc addObserver:obs selector:@selector(onScreen:) name:NSWindowDidChangeScreenNotification object:window];
        [nc addObserver:obs selector:@selector(onOcclusion:) name:NSWindowDidChangeOcclusionStateNotification object:window];
        [nc addObserver:obs selector:@selector(onBecomeKey:) name:NSWindowDidBecomeKeyNotification object:window];
        [nc addObserver:obs selector:@selector(onResignKey:) name:NSWindowDidResignKeyNotification object:window];
        [nc addObserver:obs selector:@selector(onWillClose:) name:NSWindowWillCloseNotification object:window];
        [nc addObserver:obs selector:@selector(onAppActive:) name:NSApplicationDidBecomeActiveNotification object:nil];
        [nc addObserver:obs selector:@selector(onAppResign:) name:NSApplicationDidResignActiveNotification object:nil];

        MelWindowDelegate* delegate = [[MelWindowDelegate alloc] init];
        delegate.window_handle = n->self;
        [window setDelegate:delegate];

        objc_setAssociatedObject(window, "mel_window_observer", obs, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(window, "mel_window_delegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [window setReleasedWhenClosed:NO];
        [window setTitle:mel_window__nsstring(o->title)];
        if (o->min_w > 0 || o->min_h > 0)
            [window setContentMinSize:NSMakeSize(o->min_w, o->min_h)];
        if (o->max_w > 0 || o->max_h > 0)
            [window setContentMaxSize:NSMakeSize(o->max_w, o->max_h)];
        [window center];

        n->native = (void*)CFBridgingRetain(window);
        n->scale = (f32)window.backingScaleFactor;
        n->point_w = n->w;
        n->point_h = n->h;

        if (!o->start_hidden)
        {
            [window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}

void mel_window__backend_destroy(Mel_Window_Node* n)
{
    if (!n || !n->native)
        return;
    NSWindow* window = (__bridge NSWindow*)n->native;
    [window close];
}

void mel_window_set_title(Mel_Window w, str8 title)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    NSWindow* window = (__bridge NSWindow*)n->native;
    [window setTitle:mel_window__nsstring(title)];
}

void mel_window_set_bounds(Mel_Window w, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;
    n->x = x;
    n->y = y;
    n->w = width;
    n->h = height;
    if (!n->native)
        return;

    NSWindow* window = (__bridge NSWindow*)n->native;
    NSRect    desired = [window frameRectForContentRect:NSMakeRect(0, 0, width, height)];
    NSScreen* screen = window.screen ?: [NSScreen mainScreen];
    CGFloat   sh = screen.frame.size.height;
    NSRect    fr = desired;
    fr.origin = NSMakePoint((CGFloat)x, sh - (CGFloat)y - fr.size.height);
    [window setFrame:fr display:YES];
}

void mel_window_set_visible(Mel_Window w, bool visible)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    NSWindow* window = (__bridge NSWindow*)n->native;
    if (visible)
    {
        [window makeKeyAndOrderFront:nil];
    }
    else
    {
        [window orderOut:nil];
    }
}

void mel_window_set_focus(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    NSWindow* window = (__bridge NSWindow*)n->native;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void mel_window_refresh(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    NSWindow* window = (__bridge NSWindow*)n->native;
    [[window contentView] setNeedsDisplay:YES];
}
