#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Carbon/Carbon.h>

#include <input/provider.h>
#include <input/macos/macos.h>

#include "../input_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <log/log.h>

#define MEL_MACOS_KEYBOARD_ID 0x6D61636B6264ULL
#define MEL_MACOS_MOUSE_ID    0x6D61636D7365ULL

typedef struct
{
    NSCursor* cursor;
    bool      custom;
} Cursor_Slot;

static struct
{
    bool        init;
    Mel_SlotMap cursors;
    f32         mouse_x, mouse_y;
    f32         mouse_gx, mouse_gy;
    u32         buttons;
    bool        relative;
    bool        captured;
    bool        text_active;
    u32         pressed[(MEL_SCANCODE_COUNT + 31) / 32];
} g_mac;

static void mac_set_pressed(Mel_Scancode sc, bool down)
{
    if (sc == MEL_SCANCODE_UNKNOWN || sc >= MEL_SCANCODE_COUNT)
        return;
    u32 word = (u32)sc >> 5;
    u32 bit = 1u << ((u32)sc & 31);
    if (down)
        g_mac.pressed[word] |= bit;
    else
        g_mac.pressed[word] &= ~bit;
}

static const Mel_Alloc* mac_alloc(void) { return mel_alloc_heap(); }

static void mac_ensure(void)
{
    if (g_mac.init)
        return;
    mel_slotmap_init(&g_mac.cursors, mac_alloc(), .item_size = sizeof(Cursor_Slot), .initial_capacity = 4);
    g_mac.init = true;
}

static Mel_Scancode mac_scancode_from_virtual(unsigned short vk)
{
    switch (vk)
    {
    case kVK_ANSI_A:
        return MEL_SCANCODE_A;
    case kVK_ANSI_B:
        return MEL_SCANCODE_B;
    case kVK_ANSI_C:
        return MEL_SCANCODE_C;
    case kVK_ANSI_D:
        return MEL_SCANCODE_D;
    case kVK_ANSI_E:
        return MEL_SCANCODE_E;
    case kVK_ANSI_F:
        return MEL_SCANCODE_F;
    case kVK_ANSI_G:
        return MEL_SCANCODE_G;
    case kVK_ANSI_H:
        return MEL_SCANCODE_H;
    case kVK_ANSI_I:
        return MEL_SCANCODE_I;
    case kVK_ANSI_J:
        return MEL_SCANCODE_J;
    case kVK_ANSI_K:
        return MEL_SCANCODE_K;
    case kVK_ANSI_L:
        return MEL_SCANCODE_L;
    case kVK_ANSI_M:
        return MEL_SCANCODE_M;
    case kVK_ANSI_N:
        return MEL_SCANCODE_N;
    case kVK_ANSI_O:
        return MEL_SCANCODE_O;
    case kVK_ANSI_P:
        return MEL_SCANCODE_P;
    case kVK_ANSI_Q:
        return MEL_SCANCODE_Q;
    case kVK_ANSI_R:
        return MEL_SCANCODE_R;
    case kVK_ANSI_S:
        return MEL_SCANCODE_S;
    case kVK_ANSI_T:
        return MEL_SCANCODE_T;
    case kVK_ANSI_U:
        return MEL_SCANCODE_U;
    case kVK_ANSI_V:
        return MEL_SCANCODE_V;
    case kVK_ANSI_W:
        return MEL_SCANCODE_W;
    case kVK_ANSI_X:
        return MEL_SCANCODE_X;
    case kVK_ANSI_Y:
        return MEL_SCANCODE_Y;
    case kVK_ANSI_Z:
        return MEL_SCANCODE_Z;
    case kVK_ANSI_1:
        return MEL_SCANCODE_1;
    case kVK_ANSI_2:
        return MEL_SCANCODE_2;
    case kVK_ANSI_3:
        return MEL_SCANCODE_3;
    case kVK_ANSI_4:
        return MEL_SCANCODE_4;
    case kVK_ANSI_5:
        return MEL_SCANCODE_5;
    case kVK_ANSI_6:
        return MEL_SCANCODE_6;
    case kVK_ANSI_7:
        return MEL_SCANCODE_7;
    case kVK_ANSI_8:
        return MEL_SCANCODE_8;
    case kVK_ANSI_9:
        return MEL_SCANCODE_9;
    case kVK_ANSI_0:
        return MEL_SCANCODE_0;
    case kVK_Return:
        return MEL_SCANCODE_RETURN;
    case kVK_Escape:
        return MEL_SCANCODE_ESCAPE;
    case kVK_Delete:
        return MEL_SCANCODE_BACKSPACE;
    case kVK_Tab:
        return MEL_SCANCODE_TAB;
    case kVK_Space:
        return MEL_SCANCODE_SPACE;
    case kVK_ANSI_Minus:
        return MEL_SCANCODE_MINUS;
    case kVK_ANSI_Equal:
        return MEL_SCANCODE_EQUALS;
    case kVK_ANSI_LeftBracket:
        return MEL_SCANCODE_LEFTBRACKET;
    case kVK_ANSI_RightBracket:
        return MEL_SCANCODE_RIGHTBRACKET;
    case kVK_ANSI_Backslash:
        return MEL_SCANCODE_BACKSLASH;
    case kVK_ANSI_Semicolon:
        return MEL_SCANCODE_SEMICOLON;
    case kVK_ANSI_Quote:
        return MEL_SCANCODE_APOSTROPHE;
    case kVK_ANSI_Grave:
        return MEL_SCANCODE_GRAVE;
    case kVK_ANSI_Comma:
        return MEL_SCANCODE_COMMA;
    case kVK_ANSI_Period:
        return MEL_SCANCODE_PERIOD;
    case kVK_ANSI_Slash:
        return MEL_SCANCODE_SLASH;
    case kVK_CapsLock:
        return MEL_SCANCODE_CAPSLOCK;
    case kVK_F1:
        return MEL_SCANCODE_F1;
    case kVK_F2:
        return MEL_SCANCODE_F2;
    case kVK_F3:
        return MEL_SCANCODE_F3;
    case kVK_F4:
        return MEL_SCANCODE_F4;
    case kVK_F5:
        return MEL_SCANCODE_F5;
    case kVK_F6:
        return MEL_SCANCODE_F6;
    case kVK_F7:
        return MEL_SCANCODE_F7;
    case kVK_F8:
        return MEL_SCANCODE_F8;
    case kVK_F9:
        return MEL_SCANCODE_F9;
    case kVK_F10:
        return MEL_SCANCODE_F10;
    case kVK_F11:
        return MEL_SCANCODE_F11;
    case kVK_F12:
        return MEL_SCANCODE_F12;
    case kVK_Home:
        return MEL_SCANCODE_HOME;
    case kVK_PageUp:
        return MEL_SCANCODE_PAGEUP;
    case kVK_ForwardDelete:
        return MEL_SCANCODE_DELETE;
    case kVK_End:
        return MEL_SCANCODE_END;
    case kVK_PageDown:
        return MEL_SCANCODE_PAGEDOWN;
    case kVK_RightArrow:
        return MEL_SCANCODE_RIGHT;
    case kVK_LeftArrow:
        return MEL_SCANCODE_LEFT;
    case kVK_DownArrow:
        return MEL_SCANCODE_DOWN;
    case kVK_UpArrow:
        return MEL_SCANCODE_UP;
    case kVK_Control:
        return MEL_SCANCODE_LCTRL;
    case kVK_Shift:
        return MEL_SCANCODE_LSHIFT;
    case kVK_Option:
        return MEL_SCANCODE_LALT;
    case kVK_Command:
        return MEL_SCANCODE_LGUI;
    case kVK_RightControl:
        return MEL_SCANCODE_RCTRL;
    case kVK_RightShift:
        return MEL_SCANCODE_RSHIFT;
    case kVK_RightOption:
        return MEL_SCANCODE_RALT;
    case kVK_RightCommand:
        return MEL_SCANCODE_RGUI;
    default:
        return MEL_SCANCODE_UNKNOWN;
    }
}

static u32 mac_modifiers(NSEventModifierFlags flags)
{
    u32 m = 0;
    if (flags & NSEventModifierFlagShift)
        m |= MEL_INPUT_MOD_LSHIFT;
    if (flags & NSEventModifierFlagControl)
        m |= MEL_INPUT_MOD_LCTRL;
    if (flags & NSEventModifierFlagOption)
        m |= MEL_INPUT_MOD_LALT;
    if (flags & NSEventModifierFlagCommand)
        m |= MEL_INPUT_MOD_LGUI;
    if (flags & NSEventModifierFlagCapsLock)
        m |= MEL_INPUT_MOD_CAPS;
    if (flags & NSEventModifierFlagFunction)
        m |= MEL_INPUT_MOD_MODE;
    return m;
}

static u32 mac_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (cap < 2)
        return 0;
    out[0] = (Mel_Input_Raw){
        .stable_id = MEL_MACOS_KEYBOARD_ID,
        .desc = { .name = S8("Keyboard"), .caps = MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT | MEL_INPUT_CAP_IME, .key_count = MEL_SCANCODE_COUNT },
    };
    out[1] = (Mel_Input_Raw){
        .stable_id = MEL_MACOS_MOUSE_ID,
        .desc = { .name = S8("Mouse"), .caps = MEL_INPUT_CAP_MOUSE | MEL_INPUT_CAP_RELATIVE | MEL_INPUT_CAP_CAPTURE | MEL_INPUT_CAP_WARP | MEL_INPUT_CAP_CONFINE | MEL_INPUT_CAP_CURSOR, .button_count = 5 },
    };
    return 2;
}

static bool mac_key_down(void* user, u64 stable_id, Mel_Scancode sc)
{
    (void)user;
    (void)stable_id;
    if (sc == MEL_SCANCODE_UNKNOWN || sc >= MEL_SCANCODE_COUNT)
        return false;
    return (g_mac.pressed[(u32)sc >> 5] & (1u << ((u32)sc & 31))) != 0;
}

static u32 mac_mods_now(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    return mac_modifiers([NSEvent modifierFlags]);
}

static Mel_Mouse_State mac_mouse_state(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    NSPoint loc = [NSEvent mouseLocation];
    return (Mel_Mouse_State){
        .x = g_mac.mouse_x,
        .y = g_mac.mouse_y,
        .global_x = (f32)loc.x,
        .global_y = (f32)loc.y,
        .buttons = g_mac.buttons,
        .relative = g_mac.relative,
        .captured = g_mac.captured,
    };
}

static Mel_Input_Status mac_mouse_set_relative(void* user, u64 stable_id, bool enable)
{
    (void)user;
    (void)stable_id;
    g_mac.relative = enable;
    CGAssociateMouseAndMouseCursorPosition(enable ? false : true);
    if (enable)
        CGDisplayHideCursor(kCGDirectMainDisplay);
    else
        CGDisplayShowCursor(kCGDirectMainDisplay);
    return MEL_INPUT_OK;
}

static Mel_Input_Status mac_mouse_capture(void* user, bool enable)
{
    (void)user;
    g_mac.captured = enable;
    return MEL_INPUT_OK;
}

static Mel_Input_Status mac_mouse_warp(void* user, u64 stable_id, f32 x, f32 y, bool global)
{
    (void)user;
    (void)stable_id;
    (void)global;
    CGWarpMouseCursorPosition(CGPointMake(x, y));
    return MEL_INPUT_OK;
}

static Mel_Input_Status mac_mouse_confine(void* user, const Mel_Mouse_Rect* rect)
{
    (void)user;
    (void)rect;
    return MEL_INPUT_WARNED | MEL_INPUT_CONFINE_UNAVAILABLE;
}

static NSCursor* mac_system_cursor(Mel_Cursor_Shape shape)
{
    switch (shape)
    {
    case MEL_CURSOR_IBEAM:
        return [NSCursor IBeamCursor];
    case MEL_CURSOR_CROSSHAIR:
        return [NSCursor crosshairCursor];
    case MEL_CURSOR_RESIZE_WE:
        return [NSCursor resizeLeftRightCursor];
    case MEL_CURSOR_RESIZE_NS:
        return [NSCursor resizeUpDownCursor];
    case MEL_CURSOR_POINTER:
        return [NSCursor pointingHandCursor];
    case MEL_CURSOR_NOT_ALLOWED:
        return [NSCursor operationNotAllowedCursor];
    case MEL_CURSOR_MOVE:
        return [NSCursor closedHandCursor];
    default:
        return [NSCursor arrowCursor];
    }
}

static Mel_Cursor mac_cursor_create_system(void* user, Mel_Cursor_Shape shape)
{
    (void)user;
    mac_ensure();
    Cursor_Slot        slot = { .cursor = mac_system_cursor(shape), .custom = false };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&g_mac.cursors, &slot);
    return (Mel_Cursor){ h };
}

static Mel_Cursor mac_cursor_create_custom(void* user, const Mel_Cursor_Opt* opt)
{
    (void)user;
    mac_ensure();
    if (opt->frame_count == 0 || opt->frames[0].rgba == NULL)
        return MEL_CURSOR_NULL;
    const Mel_Cursor_Frame* f = &opt->frames[0];
    NSBitmapImageRep*       rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                          pixelsWide:f->width
                                                                          pixelsHigh:f->height
                                                                       bitsPerSample:8
                                                                     samplesPerPixel:4
                                                                            hasAlpha:YES
                                                                            isPlanar:NO
                                                                      colorSpaceName:NSDeviceRGBColorSpace
                                                                         bytesPerRow:f->width * 4
                                                                        bitsPerPixel:32];
    memcpy([rep bitmapData], f->rgba, (usize)f->width * f->height * 4);
    NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(f->width, f->height)];
    [img addRepresentation:rep];
    NSCursor*          cur = [[NSCursor alloc] initWithImage:img hotSpot:NSMakePoint(opt->hotspot_x, opt->hotspot_y)];
    Cursor_Slot        slot = { .cursor = cur, .custom = true };
    Mel_SlotMap_Handle h = mel_slotmap_insert(&g_mac.cursors, &slot);
    return (Mel_Cursor){ h };
}

static void mac_cursor_destroy(void* user, Mel_Cursor c)
{
    (void)user;
    if (!g_mac.init)
        return;
    mel_slotmap_remove(&g_mac.cursors, c.h);
}

static Mel_Input_Status mac_cursor_set(void* user, Mel_Cursor c)
{
    (void)user;
    Cursor_Slot* s = (Cursor_Slot*)mel_slotmap_get(&g_mac.cursors, c.h);
    if (!s || !s->cursor)
        return MEL_INPUT_ERROR | MEL_INPUT_INVALID_HANDLE;
    [s->cursor set];
    return MEL_INPUT_OK;
}

static Mel_Input_Status mac_cursor_show(void* user, bool visible)
{
    (void)user;
    if (visible)
        [NSCursor unhide];
    else
        [NSCursor hide];
    return MEL_INPUT_OK;
}

static Mel_Input_Status mac_text_start(void* user, const Mel_Input_Text_Opt* opt)
{
    (void)user;
    (void)opt;
    g_mac.text_active = true;
    return MEL_INPUT_OK;
}

static void mac_text_stop(void* user)
{
    (void)user;
    g_mac.text_active = false;
}

static Mel_Input_Status mac_text_set_area(void* user, Mel_Input_Rect area)
{
    (void)user;
    (void)area;
    return MEL_INPUT_OK;
}

static Mel_Input_Status mac_osk(void* user)
{
    (void)user;
    return MEL_INPUT_WARNED | MEL_INPUT_UNSUPPORTED;
}

static void* mac_native(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    return NULL;
}

static Mel_Input_Provider_Desc g_desc;

void mel_input__register_host_providers(void)
{
    g_desc = (Mel_Input_Provider_Desc){
        .name = "macos-appkit",
        .enumerate = mac_enumerate,
        .key_down = mac_key_down,
        .modifiers = mac_mods_now,
        .mouse_state = mac_mouse_state,
        .mouse_set_relative = mac_mouse_set_relative,
        .mouse_capture = mac_mouse_capture,
        .mouse_warp = mac_mouse_warp,
        .mouse_confine = mac_mouse_confine,
        .cursor_create_system = mac_cursor_create_system,
        .cursor_create_custom = mac_cursor_create_custom,
        .cursor_destroy = mac_cursor_destroy,
        .cursor_set = mac_cursor_set,
        .cursor_show = mac_cursor_show,
        .text_start = mac_text_start,
        .text_stop = mac_text_stop,
        .text_set_area = mac_text_set_area,
        .osk_show = mac_osk,
        .osk_hide = mac_osk,
        .native = mac_native,
    };
    mel_input_provider_register(&g_desc);
}

void mel_input_macos_handle_nsevent(const void* nsevent)
{
    Mel_Input_Sink* sink = mel_input__sink();
    if (nsevent == NULL || sink == NULL)
        return;
    NSEvent* ev = (__bridge NSEvent*)nsevent;
    switch (ev.type)
    {
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
    {
        Mel_Scancode sc = mac_scancode_from_virtual(ev.keyCode);
        bool         down = ev.type == NSEventTypeKeyDown;
        mac_set_pressed(sc, down);
        Mel_Input_Key_Event ke = {
            .scancode = sc,
            .keycode = ev.charactersIgnoringModifiers.length ? [ev.charactersIgnoringModifiers characterAtIndex:0] : 0,
            .modifiers = mac_modifiers(ev.modifierFlags),
            .down = down,
            .repeat = down && ev.isARepeat,
        };
        mel_input_sink_key(sink, MEL_MACOS_KEYBOARD_ID, &ke);
        break;
    }
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
    {
        g_mac.mouse_x += (f32)ev.deltaX;
        g_mac.mouse_y += (f32)ev.deltaY;
        Mel_Input_Mouse_Event me = { .x = g_mac.mouse_x, .y = g_mac.mouse_y, .dx = (f32)ev.deltaX, .dy = (f32)ev.deltaY, .buttons = g_mac.buttons };
        mel_input_sink_mouse(sink, MEL_MACOS_MOUSE_ID, &me);
        break;
    }
    case NSEventTypeScrollWheel:
    {
        Mel_Input_Mouse_Event me = { .wheel_x = (f32)ev.scrollingDeltaX, .wheel_y = (f32)ev.scrollingDeltaY, .wheel_flipped = ev.isDirectionInvertedFromDevice, .buttons = g_mac.buttons };
        mel_input_sink_mouse(sink, MEL_MACOS_MOUSE_ID, &me);
        break;
    }
    default:
        break;
    }
}

NSCursor* mel_input_macos_cursor(Mel_Cursor c)
{
    if (!g_mac.init)
        return nil;
    Cursor_Slot* s = (Cursor_Slot*)mel_slotmap_get(&g_mac.cursors, c.h);
    return s ? s->cursor : nil;
}
