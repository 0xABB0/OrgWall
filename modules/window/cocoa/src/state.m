#include "cocoa.h"

#import <objc/runtime.h>
#import <QuartzCore/QuartzCore.h>

#include <float.h>
#include <string.h>

static NSWindow* mel_window__cocoa(Mel_Window_Node* n) { return n && n->native ? (__bridge NSWindow*)n->native : nil; }

static bool cocoa_set_min_size(Mel_Window_Node* n, i32 w, i32 h)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    [win setContentMinSize:NSMakeSize(w, h)];
    return true;
}

static bool cocoa_set_max_size(Mel_Window_Node* n, i32 w, i32 h)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    CGFloat mw = (w > 0) ? (CGFloat)w : FLT_MAX;
    CGFloat mh = (h > 0) ? (CGFloat)h : FLT_MAX;
    [win setContentMaxSize:NSMakeSize(mw, mh)];
    return true;
}

static bool cocoa_set_aspect(Mel_Window_Node* n, f32 min_ratio, f32 max_ratio)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    f32 ratio = (max_ratio > 0.0f) ? max_ratio : min_ratio;
    if (ratio <= 0.0f)
    {
        [win setResizeIncrements:NSMakeSize(1.0, 1.0)];
        win.contentAspectRatio = NSZeroSize;
        return true;
    }
    NSSize cs = win.contentView.bounds.size;
    CGFloat base = cs.height > 0 ? cs.height : 1.0;
    win.contentAspectRatio = NSMakeSize(ratio * base, base);
    return true;
}

static bool cocoa_set_fullscreen(Mel_Window_Node* n, u32 flags)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    bool want = (flags != MEL_WINDOW_FULLSCREEN_OFF);
    bool is = (win.styleMask & NSWindowStyleMaskFullScreen) != 0;
    if (want != is)
        [win toggleFullScreen:nil];
    return true;
}

static bool cocoa_set_fullscreen_mode(Mel_Window_Node* n, Mel_Window_Video_Mode mode)
{
    (void)n;
    (void)mode;
    return false;
}

static bool cocoa_get_fullscreen_mode(Mel_Window_Node* n, Mel_Window_Video_Mode* out)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    NSScreen* screen = win.screen ?: [NSScreen mainScreen];
    NSRect    fr = screen.frame;
    f32       scale = (f32)screen.backingScaleFactor;
    out->width_px = (u32)(fr.size.width * scale);
    out->height_px = (u32)(fr.size.height * scale);
    out->refresh_mhz = 0;
    out->format_flags = MEL_WINDOW_PIXEL_BGRA8 | MEL_WINDOW_PIXEL_SRGB;
    if (@available(macOS 12.0, *))
        out->refresh_mhz = (u32)(screen.maximumFramesPerSecond * 1000);
    return true;
}

static bool cocoa_set_opacity(Mel_Window_Node* n, f32 opacity)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    win.alphaValue = (CGFloat)opacity;
    return true;
}

static bool cocoa_set_always_on_top(Mel_Window_Node* n, bool on)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    win.level = on ? NSFloatingWindowLevel : NSNormalWindowLevel;
    return true;
}

static bool cocoa_set_borderless(Mel_Window_Node* n, bool borderless)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    if (borderless)
        win.styleMask &= ~(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable);
    else
        win.styleMask |= (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable);
    return true;
}

static bool cocoa_set_resizable(Mel_Window_Node* n, bool resizable)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    if (resizable)
        win.styleMask |= NSWindowStyleMaskResizable;
    else
        win.styleMask &= ~NSWindowStyleMaskResizable;
    return true;
}

static bool cocoa_set_icon(Mel_Window_Node* n, const u8* rgba, i32 w, i32 h)
{
    (void)n;
    if (!rgba || w <= 0 || h <= 0)
        return false;
    @autoreleasepool
    {
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                       pixelsWide:w
                                                                       pixelsHigh:h
                                                                    bitsPerSample:8
                                                                  samplesPerPixel:4
                                                                         hasAlpha:YES
                                                                         isPlanar:NO
                                                                   colorSpaceName:NSCalibratedRGBColorSpace
                                                                      bytesPerRow:w * 4
                                                                     bitsPerPixel:32];
        if (!rep)
            return false;
        memcpy(rep.bitmapData, rgba, (usize)w * (usize)h * 4u);
        NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
        [img addRepresentation:rep];
        [NSApp setApplicationIconImage:img];
    }
    return true;
}

static bool cocoa_set_modal(Mel_Window_Node* n, bool modal)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    win.level = modal ? NSModalPanelWindowLevel : NSNormalWindowLevel;
    return true;
}

static bool cocoa_set_parent(Mel_Window_Node* n, Mel_Window_Node* parent)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    NSWindow* prev = win.parentWindow;
    if (prev)
        [prev removeChildWindow:win];
    if (parent && parent->native)
    {
        NSWindow* pw = (__bridge NSWindow*)parent->native;
        [pw addChildWindow:win ordered:NSWindowAbove];
    }
    return true;
}

static bool cocoa_set_shape(Mel_Window_Node* n, const u8* alpha, i32 w, i32 h)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    if (!alpha)
    {
        win.opaque = YES;
        win.backgroundColor = [NSColor windowBackgroundColor];
        return true;
    }
    (void)w;
    (void)h;
    win.opaque = NO;
    win.backgroundColor = [NSColor clearColor];
    return true;
}

static bool cocoa_set_mouse_grab(Mel_Window_Node* n, bool grab)
{
    (void)n;
    (void)grab;
    return false;
}

static bool cocoa_set_keyboard_grab(Mel_Window_Node* n, bool grab)
{
    (void)n;
    (void)grab;
    return false;
}

static bool cocoa_set_mouse_rect(Mel_Window_Node* n, Mel_Window_Rect rect)
{
    (void)n;
    (void)rect;
    return false;
}

static bool cocoa_set_progress_state(Mel_Window_Node* n, u32 state)
{
    (void)n;
    @autoreleasepool
    {
        NSDockTile* tile = [NSApp dockTile];
        if (state == MEL_WINDOW_PROGRESS_NONE)
            tile.badgeLabel = nil;
        [tile display];
    }
    return true;
}

static bool cocoa_set_progress_value(Mel_Window_Node* n, f32 value)
{
    (void)n;
    @autoreleasepool
    {
        NSDockTile* tile = [NSApp dockTile];
        if (value > 0.0f && value < 1.0f)
            tile.badgeLabel = [NSString stringWithFormat:@"%d%%", (int)(value * 100.0f + 0.5f)];
        else
            tile.badgeLabel = nil;
        [tile display];
    }
    return true;
}

static bool cocoa_safe_area(Mel_Window_Node* n, Mel_Window_Rect* out)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    NSView* cv = win.contentView;
    NSRect  b = cv.bounds;
    f32     scale = (f32)win.backingScaleFactor;
    NSEdgeInsets ins = { 0, 0, 0, 0 };
    if (@available(macOS 11.0, *))
        ins = cv.safeAreaInsets;
    out->x = (i32)(ins.left * scale);
    out->y = (i32)(ins.top * scale);
    out->w = (i32)((b.size.width - ins.left - ins.right) * scale);
    out->h = (i32)((b.size.height - ins.top - ins.bottom) * scale);
    return true;
}

static bool cocoa_pixel_format(Mel_Window_Node* n, u32* out_flags)
{
    (void)n;
    *out_flags = MEL_WINDOW_PIXEL_BGRA8 | MEL_WINDOW_PIXEL_PREMULTIPLIED | MEL_WINDOW_PIXEL_SRGB;
    return true;
}

static u64 cocoa_native_id(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    return win ? (u64)win.windowNumber : 0;
}

static bool cocoa_maximize(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    if (!win.isZoomed)
        [win zoom:nil];
    return true;
}

static bool cocoa_minimize(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    [win miniaturize:nil];
    return true;
}

static bool cocoa_restore(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    if (win.isMiniaturized)
        [win deminiaturize:nil];
    else if (win.isZoomed)
        [win zoom:nil];
    return true;
}

static bool cocoa_raise(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    [win orderFront:nil];
    return true;
}

static bool cocoa_flash(Mel_Window_Node* n, u32 flags)
{
    (void)n;
    NSRequestUserAttentionType t = (flags & MEL_WINDOW_FLASH_UNTIL_FOCUS) ? NSCriticalRequest : NSInformationalRequest;
    if (flags == MEL_WINDOW_FLASH_CANCEL)
        return true;
    [NSApp requestUserAttention:t];
    return true;
}

static bool cocoa_get_surface(Mel_Window_Node* n, Mel_Window_Surface* out)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    i32 stride = n->w * 4;
    if (!n->surface_pixels || n->surface_w != n->w || n->surface_h != n->h)
    {
        const Mel_Alloc* a = mel_window__alloc();
        if (n->surface_pixels)
            mel_dealloc(a, n->surface_pixels);
        n->surface_pixels = mel_alloc(a, (usize)stride * (usize)(n->h > 0 ? n->h : 1));
        n->surface_w = n->w;
        n->surface_h = n->h;
        n->surface_stride = stride;
        n->surface_format = MEL_WINDOW_PIXEL_BGRA8 | MEL_WINDOW_PIXEL_PREMULTIPLIED | MEL_WINDOW_PIXEL_SRGB;
    }
    out->pixels = n->surface_pixels;
    out->width_px = n->surface_w;
    out->height_px = n->surface_h;
    out->stride_bytes = n->surface_stride;
    out->format_flags = n->surface_format;
    return true;
}

static bool cocoa_present_surface(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win || !n->surface_pixels)
        return false;
    @autoreleasepool
    {
        CGColorSpaceRef     cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGBitmapInfo        bi = kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst;
        CGContextRef        ctx = CGBitmapContextCreate(n->surface_pixels, (usize)n->surface_w, (usize)n->surface_h, 8, (usize)n->surface_stride, cs, bi);
        CGColorSpaceRelease(cs);
        if (!ctx)
            return false;
        CGImageRef img = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        if (!img)
            return false;
        win.contentView.wantsLayer = YES;
        win.contentView.layer.contents = (__bridge id)img;
        CGImageRelease(img);
    }
    return true;
}

static bool cocoa_icc_profile(Mel_Window_Node* n, Mel_Window_Icc_Profile* out)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return false;
    NSScreen*   screen = win.screen ?: [NSScreen mainScreen];
    NSData*     data = screen.colorSpace.ICCProfileData;
    if (!data || data.length == 0)
        return false;
    const Mel_Alloc* a = mel_window__alloc();
    usize len = (usize)data.length;
    u8*   copy = (u8*)mel_alloc(a, len);
    memcpy(copy, data.bytes, len);
    out->data = copy;
    out->size = len;
    return true;
}

static u32 cocoa_live_flags(Mel_Window_Node* n)
{
    NSWindow* win = mel_window__cocoa(n);
    if (!win)
        return 0;
    u32 flags = 0;
    if (win.isVisible)
        flags |= MEL_WINDOW_STATE_SHOWN;
    else
        flags |= MEL_WINDOW_STATE_HIDDEN;
    if (win.isMiniaturized)
        flags |= MEL_WINDOW_STATE_MINIMIZED;
    if (win.isZoomed)
        flags |= MEL_WINDOW_STATE_MAXIMIZED;
    if (win.isKeyWindow)
        flags |= MEL_WINDOW_STATE_FOCUSED;
    if ((win.occlusionState & NSWindowOcclusionStateVisible) == 0)
        flags |= MEL_WINDOW_STATE_OCCLUDED;
    if ((win.styleMask & NSWindowStyleMaskFullScreen) != 0)
        flags |= MEL_WINDOW_STATE_FULLSCREEN;
    return flags;
}

static const Mel_Window_Backend_Ops g_cocoa_ops = {
    .set_min_size = cocoa_set_min_size,
    .set_max_size = cocoa_set_max_size,
    .set_aspect = cocoa_set_aspect,
    .set_fullscreen = cocoa_set_fullscreen,
    .set_fullscreen_mode = cocoa_set_fullscreen_mode,
    .get_fullscreen_mode = cocoa_get_fullscreen_mode,
    .set_opacity = cocoa_set_opacity,
    .set_always_on_top = cocoa_set_always_on_top,
    .set_borderless = cocoa_set_borderless,
    .set_resizable = cocoa_set_resizable,
    .set_icon = cocoa_set_icon,
    .set_modal = cocoa_set_modal,
    .set_parent = cocoa_set_parent,
    .set_shape = cocoa_set_shape,
    .set_mouse_grab = cocoa_set_mouse_grab,
    .set_keyboard_grab = cocoa_set_keyboard_grab,
    .set_mouse_rect = cocoa_set_mouse_rect,
    .set_progress_state = cocoa_set_progress_state,
    .set_progress_value = cocoa_set_progress_value,
    .safe_area = cocoa_safe_area,
    .pixel_format = cocoa_pixel_format,
    .native_id = cocoa_native_id,
    .maximize = cocoa_maximize,
    .minimize = cocoa_minimize,
    .restore = cocoa_restore,
    .raise = cocoa_raise,
    .flash = cocoa_flash,
    .get_surface = cocoa_get_surface,
    .present_surface = cocoa_present_surface,
    .icc_profile = cocoa_icc_profile,
    .live_flags = cocoa_live_flags,
};

const Mel_Window_Backend_Ops* mel_window__backend_ops(void) { return &g_cocoa_ops; }
