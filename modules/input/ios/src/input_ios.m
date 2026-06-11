#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#include <input/provider.h>
#include <input/ios/ios.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <log/log.h>

#include "../../src/input_internal.h"

#define MEL_IOS_TOUCH_ID    0x696F73746368ULL
#define MEL_IOS_KEYBOARD_ID 0x696F736B6264ULL
#define MEL_IOS_PEN_ID      0x696F7370656EULL

static struct
{
    bool text_active;
    bool osk_visible;
} g_ios;

static Mel_Scancode ios_scancode_from_usage(long usage)
{
    if (usage >= 0x04 && usage <= 0xE7)
        return (Mel_Scancode)usage;
    return MEL_SCANCODE_UNKNOWN;
}

static u32 ios_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (cap < 2)
        return 0;
    u32 n = 0;
    out[n++] = (Mel_Input_Raw){
        .stable_id = MEL_IOS_TOUCH_ID,
        .desc = { .name = S8("Touchscreen"), .caps = MEL_INPUT_CAP_TOUCH | MEL_INPUT_CAP_PRESSURE, .touch_point_max = 11, .touch_direct = true, .pressure_max = 1.0f },
    };
    if (cap >= 3)
        out[n++] = (Mel_Input_Raw){
            .stable_id = MEL_IOS_PEN_ID,
            .desc = { .name = S8("Apple Pencil"),
                      .caps = MEL_INPUT_CAP_PEN | MEL_INPUT_CAP_PRESSURE | MEL_INPUT_CAP_TILT | MEL_INPUT_CAP_HOVER | MEL_INPUT_CAP_ROTATION,
                      .pen_button_count = 2,
                      .pressure_max = 1.0f,
                      .hover_distance_max = 0.02f },
        };
    out[n++] = (Mel_Input_Raw){
        .stable_id = MEL_IOS_KEYBOARD_ID,
        .desc = { .name = S8("Keyboard"), .caps = MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT | MEL_INPUT_CAP_IME, .key_count = MEL_SCANCODE_COUNT },
    };
    return n;
}

static Mel_Touch_State ios_touch_state(void* user, u64 sid)
{
    (void)user;
    (void)sid;
    return (Mel_Touch_State){ .direct = true };
}

static Mel_Pen_State ios_pen_state(void* user, u64 sid)
{
    (void)user;
    (void)sid;
    return (Mel_Pen_State){ 0 };
}

static Mel_Input_Status ios_text_start(void* user, const Mel_Input_Text_Opt* opt)
{
    (void)user;
    (void)opt;
    g_ios.text_active = true;
    g_ios.osk_visible = true;
    return MEL_INPUT_OK;
}

static void ios_text_stop(void* user)
{
    (void)user;
    g_ios.text_active = false;
    g_ios.osk_visible = false;
}

static Mel_Input_Status ios_text_set_area(void* user, Mel_Input_Rect area)
{
    (void)user;
    (void)area;
    return MEL_INPUT_OK;
}

static Mel_Input_Status ios_osk_show(void* user)
{
    (void)user;
    g_ios.osk_visible = true;
    return MEL_INPUT_OK;
}

static Mel_Input_Status ios_osk_hide(void* user)
{
    (void)user;
    g_ios.osk_visible = false;
    return MEL_INPUT_OK;
}

static Mel_Input_Provider_Desc g_desc;

void mel_input__register_host_providers(void)
{
    g_desc = (Mel_Input_Provider_Desc){
        .name = "ios-uikit",
        .enumerate = ios_enumerate,
        .touch_state = ios_touch_state,
        .pen_state = ios_pen_state,
        .text_start = ios_text_start,
        .text_stop = ios_text_stop,
        .text_set_area = ios_text_set_area,
        .osk_show = ios_osk_show,
        .osk_hide = ios_osk_hide,
    };
    mel_input_provider_register(&g_desc);
}

void mel_input_ios_handle_touches(const void* touches, const void* event)
{
    Mel_Input_Sink* sink = mel_input__sink();
    if (touches == NULL || sink == NULL)
        return;
    NSSet<UITouch*>* set = (__bridge NSSet<UITouch*>*)touches;
    (void)event;
    for (UITouch* t in set)
    {
        CGPoint p = [t locationInView:t.view];
        u32     phase;
        switch (t.phase)
        {
        case UITouchPhaseBegan:
            phase = MEL_INPUT_TOUCH_DOWN;
            break;
        case UITouchPhaseMoved:
            phase = MEL_INPUT_TOUCH_MOVE;
            break;
        case UITouchPhaseEnded:
            phase = MEL_INPUT_TOUCH_UP;
            break;
        case UITouchPhaseCancelled:
            phase = MEL_INPUT_TOUCH_CANCEL;
            break;
        default:
            phase = MEL_INPUT_TOUCH_MOVE;
            break;
        }
        CGSize sz = t.view ? t.view.bounds.size : CGSizeMake(1, 1);
        if (t.type == UITouchTypePencil)
        {
            f32                 azimuth = (f32)[t azimuthAngleInView:t.view];
            Mel_Input_Pen_Event pe = {
                .phase = phase == MEL_INPUT_TOUCH_DOWN ? MEL_INPUT_PEN_DOWN : (phase == MEL_INPUT_TOUCH_UP ? MEL_INPUT_PEN_UP : MEL_INPUT_PEN_MOVE),
                .x = sz.width > 0 ? (f32)(p.x / sz.width) : 0.0f,
                .y = sz.height > 0 ? (f32)(p.y / sz.height) : 0.0f,
                .pressure = (f32)t.force,
                .tilt_x = (f32)(M_PI_2 - t.altitudeAngle),
                .rotation = azimuth,
                .in_proximity = true,
            };
            mel_input_sink_pen(sink, MEL_IOS_PEN_ID, &pe);
        }
        else
        {
            Mel_Input_Touch_Event te = {
                .finger_id = (u64)(usize)(__bridge void*)t,
                .phase = phase,
                .x = sz.width > 0 ? (f32)(p.x / sz.width) : 0.0f,
                .y = sz.height > 0 ? (f32)(p.y / sz.height) : 0.0f,
                .pressure = (f32)t.force,
                .direct = true,
            };
            mel_input_sink_touch(sink, MEL_IOS_TOUCH_ID, &te);
        }
    }
}

void mel_input_ios_handle_presses(const void* presses, const void* event)
{
    Mel_Input_Sink* sink = mel_input__sink();
    if (presses == NULL || sink == NULL)
        return;
    NSSet<UIPress*>* set = (__bridge NSSet<UIPress*>*)presses;
    (void)event;
    if (@available(iOS 13.4, *))
    {
        for (UIPress* press in set)
        {
            UIKey* key = press.key;
            if (key == nil)
                continue;
            Mel_Input_Key_Event ke = {
                .scancode = ios_scancode_from_usage((long)key.keyCode),
                .keycode = key.characters.length ? [key.characters characterAtIndex:0] : 0,
                .down = press.phase == UIPressPhaseBegan,
            };
            mel_input_sink_key(sink, MEL_IOS_KEYBOARD_ID, &ke);
        }
    }
}
