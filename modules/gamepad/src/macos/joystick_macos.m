#include <gamepad/provider.h>
#include <gamepad/protocol.h>
#include <gamepad/macos/macos.h>

#include "../joystick_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>
#include <log/log.h>

#import <GameController/GameController.h>
#import <Foundation/Foundation.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>

typedef struct
{
    void*          controller_ref;
    u64            stable_id;
    char           name[128];
    char           serial[64];
    Mel_Array(i16) axes;
    Mel_Array(u8)  buttons;
} Mac_Pad;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Mac_Pad) pads;
    bool             discovery_started;
} Mac_Backend;

static Mac_Backend g_backend;

static GCController* controller_of(Mac_Pad* pad) { return (__bridge GCController*)pad->controller_ref; }

static Mac_Pad* pad_for(u64 stable_id)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
        if (g_backend.pads.items[i].stable_id == stable_id)
            return &g_backend.pads.items[i];
    return NULL;
}

typedef struct
{
    u64  registry_id;
    u16  vendor_id;
    u16  product_id;
    u16  version;
    char serial[64];
    bool serial_present;
    bool claimed;
} Hid_Record;

static i32 hid_int_property(IOHIDDeviceRef dev, CFStringRef key)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, key);
    if (ref == NULL || CFGetTypeID(ref) != CFNumberGetTypeID())
        return -1;
    int v = 0;
    CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &v);
    return v;
}

static bool hid_string_property(IOHIDDeviceRef dev, CFStringRef key, char* out, usize cap)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, key);
    if (ref == NULL || CFGetTypeID(ref) != CFStringGetTypeID())
        return false;
    return CFStringGetCString((CFStringRef)ref, out, (CFIndex)cap, kCFStringEncodingUTF8);
}

static u32 hid_collect(Hid_Record** out_recs, const Mel_Alloc* alloc)
{
    *out_recs = NULL;
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (mgr == NULL)
        return 0;
    IOHIDManagerSetDeviceMatching(mgr, NULL);
    CFSetRef devices = IOHIDManagerCopyDevices(mgr);
    if (devices == NULL)
    {
        CFRelease(mgr);
        return 0;
    }
    CFIndex count = CFSetGetCount(devices);
    Mel_Array(Hid_Record) recs;
    mel_array_init(&recs, alloc);
    if (count > 0)
    {
        const void** values = (const void**)mel_alloc(alloc, sizeof(void*) * (usize)count);
        CFSetGetValues(devices, values);
        for (CFIndex i = 0; i < count; i++)
        {
            IOHIDDeviceRef dev = (IOHIDDeviceRef)values[i];
            i32            usage_page = hid_int_property(dev, CFSTR(kIOHIDPrimaryUsagePageKey));
            i32            usage = hid_int_property(dev, CFSTR(kIOHIDPrimaryUsageKey));
            if (usage_page != kHIDPage_GenericDesktop)
                continue;
            if (usage != kHIDUsage_GD_Joystick && usage != kHIDUsage_GD_GamePad && usage != kHIDUsage_GD_MultiAxisController)
                continue;
            io_service_t svc = IOHIDDeviceGetService(dev);
            uint64_t     entry_id = 0;
            if (svc == MACH_PORT_NULL || IORegistryEntryGetRegistryEntryID(svc, &entry_id) != KERN_SUCCESS)
                continue;
            Hid_Record r = { 0 };
            r.registry_id = entry_id;
            i32 vid = hid_int_property(dev, CFSTR(kIOHIDVendorIDKey));
            i32 pid = hid_int_property(dev, CFSTR(kIOHIDProductIDKey));
            i32 ver = hid_int_property(dev, CFSTR(kIOHIDVersionNumberKey));
            r.vendor_id = vid > 0 ? (u16)vid : 0;
            r.product_id = pid > 0 ? (u16)pid : 0;
            r.version = ver > 0 ? (u16)ver : 0;
            r.serial_present = hid_string_property(dev, CFSTR(kIOHIDSerialNumberKey), r.serial, sizeof r.serial) && r.serial[0] != '\0';
            mel_array_push(&recs, r);
        }
        mel_dealloc(alloc, values);
    }
    CFRelease(devices);
    CFRelease(mgr);
    *out_recs = recs.items;
    return (u32)recs.count;
}

static void release_hid_records(Hid_Record* recs, const Mel_Alloc* alloc)
{
    if (recs != NULL)
        mel_dealloc(alloc, recs);
}

static Hid_Record* match_hid(Hid_Record* recs, u32 nrecs, GCController* c)
{
    (void)c;
    for (u32 i = 0; i < nrecs; i++)
    {
        if (recs[i].claimed)
            continue;
        recs[i].claimed = true;
        return &recs[i];
    }
    return NULL;
}

static void pad_fill_descriptor(GCController* c, Mac_Pad* pad, Hid_Record* hid, Mel_Joystick_Descriptor* d)
{
    *d = (Mel_Joystick_Descriptor){ 0 };
    const char* vn = c.vendorName.UTF8String;
    if (vn)
    {
        strncpy(pad->name, vn, sizeof pad->name - 1);
        d->name = str8_from_cstr(pad->name);
    }
    if (hid != NULL)
    {
        d->vendor_id = hid->vendor_id;
        d->product_id = hid->product_id;
        d->version = hid->version;
        d->guid = mel_guid_from_hidapi(3, hid->vendor_id, hid->product_id, hid->version, pad->name, 0, 0);
        if (hid->serial_present)
        {
            strncpy(pad->serial, hid->serial, sizeof pad->serial - 1);
            d->serial = str8_from_cstr(pad->serial);
        }
    }
    else
    {
        d->guid = mel_guid_from_hidapi(3, 0, 0, 0, pad->name, 0, 0);
    }
    d->player_index = (i32)c.playerIndex;

    if (c.extendedGamepad)
    {
        d->axis_count = 6;
        d->button_count = MEL_GAMEPAD_BUTTON_COUNT;
        d->features.dual_motor_rumble = false;
        d->features.trigger_rumble = false;
        d->features.player_led = true;
        if (@available(macOS 11.0, *))
        {
            d->features.gyro = (c.motion != nil && c.motion.hasRotationRate);
            d->features.accel = (c.motion != nil);
        }
    }

    if (@available(macOS 11.0, *))
    {
        if (c.battery)
        {
            d->power.has_battery = true;
            d->power.battery_level = c.battery.batteryLevel;
            d->power.charging = (c.battery.batteryState == GCDeviceBatteryStateCharging);
            d->power.wireless = true;
        }
    }
}

static void pad_reset_state(Mac_Pad* pad, u32 axis_count, u32 button_count)
{
    mel_array_clear(&pad->axes);
    mel_array_clear(&pad->buttons);
    for (u32 i = 0; i < axis_count; i++)
        mel_array_push(&pad->axes, (i16)0);
    for (u32 i = 0; i < button_count; i++)
        mel_array_push(&pad->buttons, (u8)0);
}

static void pads_clear(void)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
    {
        mel_array_free(&g_backend.pads.items[i].axes);
        mel_array_free(&g_backend.pads.items[i].buttons);
    }
    mel_array_clear(&g_backend.pads);
}

static u64 derive_stable_id(GCController* c, Hid_Record* hid)
{
    if (hid != NULL)
        return hid->registry_id;
    return 0x600d600d00000000ull | (u64)(uintptr_t)(__bridge void*)c;
}

static u32 mac_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    pads_clear();

    Hid_Record* recs = NULL;
    u32         nrecs = hid_collect(&recs, g_backend.alloc);

    u32 n = 0;
    for (GCController* c in GCController.controllers)
    {
        if (n >= cap)
            break;
        Hid_Record* hid = match_hid(recs, nrecs, c);

        Mac_Pad pad = { 0 };
        pad.controller_ref = (__bridge void*)c;
        pad.stable_id = derive_stable_id(c, hid);
        mel_array_init(&pad.axes, g_backend.alloc);
        mel_array_init(&pad.buttons, g_backend.alloc);

        Mel_Joystick_Descriptor d;
        pad_fill_descriptor(c, &pad, hid, &d);
        pad_reset_state(&pad, d.axis_count, d.button_count);

        mel_array_push(&g_backend.pads, pad);

        out[n].stable_id = pad.stable_id;
        out[n].desc = d;
        n++;
    }
    release_hid_records(recs, g_backend.alloc);
    return n;
}

static i16 norm(float v) { return (i16)(v * 32767.0f); }

static void set_button(Mac_Pad* pad, u32 idx, bool pressed)
{
    if (idx < pad->buttons.count)
        pad->buttons.items[idx] = pressed ? 1 : 0;
}

static bool mac_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    (void)user;
    Mac_Pad* pad = pad_for(stable_id);
    if (!pad)
        return false;
    GCController* c = controller_of(pad);
    if (!c)
        return false;
    GCExtendedGamepad* g = c.extendedGamepad;
    *out = (Mel_Joystick_State){ 0 };
    if (!g)
        return true;

    if (pad->axes.count >= 6)
    {
        pad->axes.items[0] = norm(g.leftThumbstick.xAxis.value);
        pad->axes.items[1] = norm(-g.leftThumbstick.yAxis.value);
        pad->axes.items[2] = norm(g.rightThumbstick.xAxis.value);
        pad->axes.items[3] = norm(-g.rightThumbstick.yAxis.value);
        pad->axes.items[4] = norm(g.leftTrigger.value);
        pad->axes.items[5] = norm(g.rightTrigger.value);
    }

    set_button(pad, MEL_GAMEPAD_BUTTON_SOUTH, g.buttonA.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_EAST, g.buttonB.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_WEST, g.buttonX.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_NORTH, g.buttonY.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_LEFT_SHOULDER, g.leftShoulder.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_RIGHT_SHOULDER, g.rightShoulder.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_UP, g.dpad.up.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_DOWN, g.dpad.down.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_LEFT, g.dpad.left.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_RIGHT, g.dpad.right.pressed);
    if (g.buttonMenu)
        set_button(pad, MEL_GAMEPAD_BUTTON_START, g.buttonMenu.pressed);
    if (g.buttonOptions)
        set_button(pad, MEL_GAMEPAD_BUTTON_BACK, g.buttonOptions.pressed);
    if (g.leftThumbstickButton)
        set_button(pad, MEL_GAMEPAD_BUTTON_LEFT_STICK, g.leftThumbstickButton.pressed);
    if (g.rightThumbstickButton)
        set_button(pad, MEL_GAMEPAD_BUTTON_RIGHT_STICK, g.rightThumbstickButton.pressed);

    out->axes = pad->axes.items;
    out->axis_count = (u32)pad->axes.count;
    out->buttons = pad->buttons.items;
    out->button_count = (u32)pad->buttons.count;

    if (@available(macOS 11.0, *))
    {
        if (c.motion)
        {
            out->has_accel = true;
            out->accel_m_s2 = (Mel_Joystick_Vec3){ (f32)c.motion.acceleration.x, (f32)c.motion.acceleration.y, (f32)c.motion.acceleration.z };
            if (c.motion.hasRotationRate)
            {
                out->has_gyro = true;
                out->gyro_rad_s = (Mel_Joystick_Vec3){ (f32)c.motion.rotationRate.x, (f32)c.motion.rotationRate.y, (f32)c.motion.rotationRate.z };
            }
        }
    }
    return true;
}

static Mel_Joystick_Status mac_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    (void)user;
    (void)rumble;
    Mac_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    GCController* c = controller_of(pad);
    if (!c)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
}

static Mel_Joystick_Status mac_set_player_index(void* user, u64 stable_id, i32 player_index)
{
    (void)user;
    Mac_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    GCController* c = controller_of(pad);
    if (!c)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    c.playerIndex = (GCControllerPlayerIndex)player_index;
    return MEL_JOYSTICK_OK;
}

static void* mac_native(void* user, u64 stable_id)
{
    (void)user;
    Mac_Pad* pad = pad_for(stable_id);
    return pad ? pad->controller_ref : NULL;
}

void mel_joystick__register_host_providers(const Mel_Alloc* alloc)
{
    g_backend.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_backend.pads, g_backend.alloc);
    if (!g_backend.discovery_started)
    {
        [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{
        }];
        g_backend.discovery_started = true;
    }
    Mel_Joystick_Provider_Desc desc = {
        .name = "gamecontroller",
        .enumerate = mac_enumerate,
        .poll = mac_poll,
        .rumble = mac_rumble,
        .set_player_index = mac_set_player_index,
        .native = mac_native,
    };
    mel_joystick_provider_register(&desc);
}

GCController* mel_joystick_macos_controller(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return nil;
    Mac_Pad* pad = pad_for(stable_id);
    return pad ? controller_of(pad) : nil;
}

void* mel_joystick_macos_iohid_device(Mel_Joystick j)
{
    (void)j;
    return NULL;
}
