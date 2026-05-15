#import <Cocoa/Cocoa.h>

#include <gui.platform.macos/gui.platform.macos.h>
#include <gui.platform/gui.platform.h>
#include <gui/gui.h>

#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <collection.array/array.h>
#include <string/string.str8.h>

@interface MelFlippedView : NSView
@end
@implementation MelFlippedView
- (BOOL)isFlipped { return YES; }
@end

typedef struct {
    Mel_Atom                 atom;
    Mel_Gui_Macos_Construct  cb;
} Mel_Macos_Ctor;

static NSWindow* mel__macos_window;
static NSView*   mel__macos_root;
static Mel_Array(Mel_Macos_Ctor) mel__macos_ctors;
static bool mel__macos_ctors_inited;
static bool mel__macos_exit_inflight;
static bool mel__macos_in_should_terminate;

static void mel__macos_ensure_ctors(void)
{
    if (!mel__macos_ctors_inited) {
        mel_array_init(&mel__macos_ctors, mel_alloc_heap());
        mel__macos_ctors_inited = true;
    }
}

bool mel_gui_macos_register_constructor(Mel_Atom atom, Mel_Gui_Macos_Construct cb)
{
    if (atom == MEL_ATOM_NONE || cb == NULL) return false;
    mel__macos_ensure_ctors();
    for (usize i = 0; i < mel__macos_ctors.count; i++) {
        if (mel__macos_ctors.items[i].atom == atom) {
            mel__macos_ctors.items[i].cb = cb;
            return true;
        }
    }
    mel_array_push(&mel__macos_ctors, ((Mel_Macos_Ctor){ .atom = atom, .cb = cb }));
    return true;
}

static Mel_Gui_Macos_Construct mel__macos_lookup_ctor(Mel_Atom atom)
{
    if (!mel__macos_ctors_inited) return NULL;
    for (usize i = 0; i < mel__macos_ctors.count; i++) {
        if (mel__macos_ctors.items[i].atom == atom) return mel__macos_ctors.items[i].cb;
    }
    return NULL;
}

NSWindow* mel_gui_macos_window(void) { return mel__macos_window; }
NSView*   mel_gui_macos_root(void)   { return mel__macos_root; }

static void mel__macos_install_menu(void)
{
    NSMenu* mainMenu = [[NSMenu alloc] init];
    [NSApp setMainMenu:mainMenu];

    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appMenuItem];

    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenuItem setSubmenu:appMenu];

    NSString* appName = [[NSProcessInfo processInfo] processName];
    [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:appName]
                       action:@selector(hide:)
                keyEquivalent:@"h"];
    NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"Hide Others"
                                                action:@selector(hideOtherApplications:)
                                         keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];
    [appMenu addItemWithTitle:@"Show All"
                       action:@selector(unhideAllApplications:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:appName]
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
}

@interface MelAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation MelAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_CREATE, 0, 0);
}
- (void)applicationDidBecomeActive:(NSNotification*)notification
{
    (void)notification;
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_RESUME, 0, 0);
}
- (void)applicationWillResignActive:(NSNotification*)notification
{
    (void)notification;
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_PAUSE, 0, 0);
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    mel__macos_in_should_terminate = true;
    mel__macos_exit_inflight = false;
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_BACK, 0, 0);
    mel__macos_in_should_terminate = false;
    if (mel__macos_exit_inflight) {
        mel__macos_exit_inflight = false;
        return NSTerminateNow;
    }
    return NSTerminateCancel;
}
- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_DESTROY, 0, 0);
    mel_gui_destroy_all_roots();
    mel_gui_shutdown();
}
@end

void mel_gui_macos_dispatch_main(void (*setup)(void))
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        static MelAppDelegate* delegate;
        delegate = [[MelAppDelegate alloc] init];
        [NSApp setDelegate:delegate];

        mel__macos_install_menu();

        NSRect frame = NSMakeRect(0, 0, 480, 640);
        NSUInteger style = NSWindowStyleMaskTitled
                         | NSWindowStyleMaskClosable
                         | NSWindowStyleMaskMiniaturizable
                         | NSWindowStyleMaskResizable;
        mel__macos_window = [[NSWindow alloc] initWithContentRect:frame
                                                        styleMask:style
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
        [mel__macos_window setTitle:@"Melody"];
        [mel__macos_window center];

        mel__macos_root = [[MelFlippedView alloc] initWithFrame:frame];
        [mel__macos_window setContentView:mel__macos_root];

        [mel__macos_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        if (setup) setup();

        [NSApp run];
    }
}

bool mel_gui_platform_init(void)     { return true; }
void mel_gui_platform_shutdown(void) {}

void* mel_gui_platform_create(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, Mel_Atom platform_class)
{
    Mel_Gui_Macos_Construct cb = mel__macos_lookup_ctor(platform_class);
    if (cb == NULL) return NULL;

    NSView* v = cb(h, desc);
    if (v == nil) return NULL;

    NSView* parent_view;
    if (!mel_gui_handle_is_none(desc->parent)) {
        void* p = mel_gui_platform_native(desc->parent);
        parent_view = p != NULL ? (__bridge NSView*)p : nil;
    } else {
        parent_view = mel__macos_root;
    }

    if (parent_view != nil && v.superview == nil) {
        NSRect frame = v.frame;
        if (desc->x != MEL_GUI_DEFAULT_POSITION) frame.origin.x = (CGFloat)desc->x;
        if (desc->y != MEL_GUI_DEFAULT_POSITION) frame.origin.y = (CGFloat)desc->y;
        if (desc->w > 0) frame.size.width  = (CGFloat)desc->w;
        if (desc->h > 0) frame.size.height = (CGFloat)desc->h;
        [v setFrame:frame];
        [parent_view addSubview:v];
    }

    return (__bridge_retained void*)v;
}

void mel_gui_platform_destroy(Mel_Gui_Handle h)
{
    void* native = mel_gui_platform_native(h);
    if (native == NULL) return;
    NSView* v = (__bridge_transfer NSView*)native;
    [v removeFromSuperview];
    mel_gui_platform_bind_native(h, NULL);
    (void)v;
}

bool mel_gui_platform_set_window_pos(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt, u32 flags)
{
    void* native = mel_gui_platform_native(h);
    if (native == NULL) return false;
    NSView* v = (__bridge NSView*)native;

    if ((flags & (MEL_GUI_SWP_NOMOVE | MEL_GUI_SWP_NOSIZE)) != (MEL_GUI_SWP_NOMOVE | MEL_GUI_SWP_NOSIZE)) {
        NSRect r = v.frame;
        if (!(flags & MEL_GUI_SWP_NOMOVE)) { r.origin.x = x; r.origin.y = y; }
        if (!(flags & MEL_GUI_SWP_NOSIZE)) { r.size.width = w; r.size.height = hgt; }
        [v setFrame:r];
    }
    if (flags & MEL_GUI_SWP_SHOW)    [v setHidden:NO];
    if (flags & MEL_GUI_SWP_HIDE)    [v setHidden:YES];
    if (flags & MEL_GUI_SWP_ENABLE)  if ([v respondsToSelector:@selector(setEnabled:)]) [(NSControl*)v setEnabled:YES];
    if (flags & MEL_GUI_SWP_DISABLE) if ([v respondsToSelector:@selector(setEnabled:)]) [(NSControl*)v setEnabled:NO];
    return true;
}

bool mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text)
{
    void* native = mel_gui_platform_native(h);
    if (native == NULL) return false;
    NSView* v = (__bridge NSView*)native;

    NSString* s = [[NSString alloc] initWithBytes:text.data length:(NSUInteger)text.len encoding:NSUTF8StringEncoding];
    if ([v respondsToSelector:@selector(setStringValue:)]) {
        [(NSTextField*)v setStringValue:(s ?: @"")];
    } else if ([v respondsToSelector:@selector(setTitle:)]) {
        [(NSButton*)v setTitle:(s ?: @"")];
    }
    return true;
}

bool mel_gui_platform_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        mel_gui_send_message(h, msg, w, l);
    });
    return true;
}

void mel_gui_platform_request_exit(void)
{
    if (mel__macos_in_should_terminate) {
        mel__macos_exit_inflight = true;
        return;
    }
    mel__macos_exit_inflight = true;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (mel__macos_exit_inflight) {
            mel__macos_exit_inflight = false;
            [NSApp terminate:nil];
        }
    });
}

bool mel_gui_app_start_activity(str8 activity_name)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    u8* buf = mel_alloc(alloc, (usize)activity_name.len);
    if (buf == NULL) return false;
    memcpy(buf, activity_name.data, (usize)activity_name.len);
    str8 owned = (str8){ .data = buf, .len = activity_name.len };

    dispatch_async(dispatch_get_main_queue(), ^{
        mel_gui_destroy_all_roots();
        mel_gui_app_build_activity(owned);
        mel_dealloc(alloc, owned.data);
    });
    return true;
}
