#import <CoreFoundation/CoreFoundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hid/IOHIDKeys.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>

#include <hid/hid.h>
#include <hid/provider.h>

#include <hid/macos/macos.h>

#include "../hid_internal.h"

#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>

#define MEL_HID_APPLE_REPORT_CAP 64

typedef struct
{
    u64            stable_id;
    IOHIDDeviceRef device;

    u8    inbuf[MEL_HID_APPLE_REPORT_CAP];
    usize report_cap;

    Mel_Array(u8) queue;
    Mel_Array(usize) queue_lens;

    CFRunLoopRef run_loop;
    bool         open;
} Apple_Device;

typedef struct
{
    const Mel_Alloc* alloc;
    IOHIDManagerRef  manager;
    Mel_Array(Apple_Device*) devices;
} Apple_Backend;

static Apple_Backend g_apple;

static u32 cf_number_u32(IOHIDDeviceRef dev, CFStringRef key)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, key);
    if (!ref || CFGetTypeID(ref) != CFNumberGetTypeID())
        return 0;
    int v = 0;
    CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &v);
    return (u32)v;
}

static void cf_string_copy(IOHIDDeviceRef dev, CFStringRef key, char* out, usize cap)
{
    out[0] = '\0';
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, key);
    if (!ref || CFGetTypeID(ref) != CFStringGetTypeID())
        return;
    CFStringGetCString((CFStringRef)ref, out, (CFIndex)cap, kCFStringEncodingUTF8);
}

static Mel_Hid_Bus transport_bus(IOHIDDeviceRef dev)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDTransportKey));
    if (!ref || CFGetTypeID(ref) != CFStringGetTypeID())
        return MEL_HID_BUS_UNKNOWN;
    CFStringRef s = (CFStringRef)ref;
    if (CFStringCompare(s, CFSTR(kIOHIDTransportUSBValue), 0) == kCFCompareEqualTo)
        return MEL_HID_BUS_USB;
    if (CFStringCompare(s, CFSTR(kIOHIDTransportBluetoothValue), 0) == kCFCompareEqualTo)
        return MEL_HID_BUS_BLUETOOTH;
    if (CFStringCompare(s, CFSTR(kIOHIDTransportBluetoothLowEnergyValue), 0) == kCFCompareEqualTo)
        return MEL_HID_BUS_BLUETOOTH;
    if (CFStringCompare(s, CFSTR(kIOHIDTransportI2CValue), 0) == kCFCompareEqualTo)
        return MEL_HID_BUS_I2C;
    if (CFStringCompare(s, CFSTR(kIOHIDTransportSPIValue), 0) == kCFCompareEqualTo)
        return MEL_HID_BUS_SPI;
    return MEL_HID_BUS_UNKNOWN;
}

static u64 stable_id_for(IOHIDDeviceRef dev)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDLocationIDKey));
    u64       loc = 0;
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID())
    {
        long long v = 0;
        CFNumberGetValue((CFNumberRef)ref, kCFNumberLongLongType, &v);
        loc = (u64)v;
    }
    u64 vid = cf_number_u32(dev, CFSTR(kIOHIDVendorIDKey));
    u64 pid = cf_number_u32(dev, CFSTR(kIOHIDProductIDKey));
    u64 up = cf_number_u32(dev, CFSTR(kIOHIDPrimaryUsagePageKey));
    u64 us = cf_number_u32(dev, CFSTR(kIOHIDPrimaryUsageKey));
    return (loc << 16) ^ (vid << 48) ^ (pid << 32) ^ (up << 8) ^ us ^ (u64)(uintptr_t)dev;
}

static void fill_descriptor(IOHIDDeviceRef dev, u64 stable_id, Mel_Hid_Descriptor* d)
{
    memset(d, 0, sizeof *d);
    d->vendor_id = (u16)cf_number_u32(dev, CFSTR(kIOHIDVendorIDKey));
    d->product_id = (u16)cf_number_u32(dev, CFSTR(kIOHIDProductIDKey));
    d->version_bcd = (u16)cf_number_u32(dev, CFSTR(kIOHIDVersionNumberKey));
    d->usage_page = (u16)cf_number_u32(dev, CFSTR(kIOHIDPrimaryUsagePageKey));
    d->usage = (u16)cf_number_u32(dev, CFSTR(kIOHIDPrimaryUsageKey));
    d->bus = transport_bus(dev);
    d->input_report_len = (u16)cf_number_u32(dev, CFSTR(kIOHIDMaxInputReportSizeKey));
    d->output_report_len = (u16)cf_number_u32(dev, CFSTR(kIOHIDMaxOutputReportSizeKey));
    d->feature_report_len = (u16)cf_number_u32(dev, CFSTR(kIOHIDMaxFeatureReportSizeKey));
    cf_string_copy(dev, CFSTR(kIOHIDManufacturerKey), d->manufacturer, MEL_HID_STRING_CAP);
    cf_string_copy(dev, CFSTR(kIOHIDProductKey), d->product, MEL_HID_STRING_CAP);
    cf_string_copy(dev, CFSTR(kIOHIDSerialNumberKey), d->serial, MEL_HID_STRING_CAP);
    snprintf(d->path, MEL_HID_STRING_CAP, "IOHIDDevice:%llu", (unsigned long long)stable_id);
}

static Apple_Device* find_device(u64 stable_id)
{
    for (usize i = 0; i < g_apple.devices.count; i++)
        if (g_apple.devices.items[i]->stable_id == stable_id)
            return g_apple.devices.items[i];
    return NULL;
}

static u32 apple_enumerate(void* user, Mel_Hid_Raw* out, u32 cap)
{
    Apple_Backend* be = user;
    if (!be->manager)
        return 0;
    CFSetRef set = IOHIDManagerCopyDevices(be->manager);
    if (!set)
        return 0;
    CFIndex n = CFSetGetCount(set);
    if (n <= 0)
    {
        CFRelease(set);
        return 0;
    }
    const void** refs = (const void**)mel_alloc(be->alloc, sizeof(void*) * (usize)n);
    if (!refs)
    {
        CFRelease(set);
        return 0;
    }
    CFSetGetValues(set, refs);

    u32 written = 0;
    for (CFIndex i = 0; i < n && written < cap; i++)
    {
        IOHIDDeviceRef dev = (IOHIDDeviceRef)refs[i];
        u64            id = stable_id_for(dev);
        Apple_Device*  ad = find_device(id);
        if (!ad)
        {
            ad = mel_alloc_type(be->alloc, Apple_Device);
            memset(ad, 0, sizeof *ad);
            ad->stable_id = id;
            ad->device = (IOHIDDeviceRef)CFRetain(dev);
            mel_array_init(&ad->queue, be->alloc);
            mel_array_init(&ad->queue_lens, be->alloc);
            mel_array_push(&be->devices, ad);
        }
        Mel_Hid_Raw* r = &out[written++];
        r->stable_id = id;
        fill_descriptor(dev, id, &r->desc);
    }

    mel_dealloc(be->alloc, refs);
    CFRelease(set);
    return written;
}

static void input_report_cb(void* context, IOReturn result, void* sender, IOHIDReportType type, uint32_t report_id, uint8_t* report, CFIndex report_len)
{
    (void)result;
    (void)sender;
    (void)type;
    (void)report_id;
    Apple_Device* ad = context;
    for (CFIndex i = 0; i < report_len; i++)
        mel_array_push(&ad->queue, report[i]);
    mel_array_push(&ad->queue_lens, (usize)report_len);
}

static Mel_Hid_Status apple_open(void* user, u64 stable_id, Mel_Hid_Channel* out_channel)
{
    (void)user;
    Apple_Device* ad = find_device(stable_id);
    if (!ad)
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    IOReturn rc = IOHIDDeviceOpen(ad->device, kIOHIDOptionsTypeNone);
    if (rc != kIOReturnSuccess)
        return MEL_HID_ERROR | (rc == kIOReturnNotPermitted ? MEL_HID_ACCESS_DENIED : 0u);
    u32 max_in = cf_number_u32(ad->device, CFSTR(kIOHIDMaxInputReportSizeKey));
    ad->report_cap = max_in > 0 && max_in <= MEL_HID_APPLE_REPORT_CAP ? max_in : MEL_HID_APPLE_REPORT_CAP;
    IOHIDDeviceRegisterInputReportCallback(ad->device, ad->inbuf, (CFIndex)ad->report_cap, input_report_cb, ad);
    ad->run_loop = CFRunLoopGetCurrent();
    IOHIDDeviceScheduleWithRunLoop(ad->device, ad->run_loop, kCFRunLoopDefaultMode);
    ad->open = true;
    *out_channel = (Mel_Hid_Channel){ .value = ad, .fd = MEL_HID_NO_FD, .bus = transport_bus(ad->device) };
    return MEL_HID_OK;
}

static void apple_close(void* user, u64 stable_id, Mel_Hid_Channel channel)
{
    (void)user;
    (void)stable_id;
    Apple_Device* ad = channel.value;
    if (!ad || !ad->open)
        return;
    if (ad->run_loop)
        IOHIDDeviceUnscheduleFromRunLoop(ad->device, ad->run_loop, kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(ad->device, ad->inbuf, (CFIndex)ad->report_cap, NULL, NULL);
    IOHIDDeviceClose(ad->device, kIOHIDOptionsTypeNone);
    ad->open = false;
    ad->run_loop = NULL;
    mel_array_clear(&ad->queue);
    mel_array_clear(&ad->queue_lens);
}

static Mel_Hid_Io_Result apple_write(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    Apple_Device* ad = channel.value;
    if (!ad || !ad->open || len == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    u8       report_id = data[0];
    usize    skip = report_id != 0 ? 1 : 0;
    IOReturn rc = IOHIDDeviceSetReport(ad->device, kIOHIDReportTypeOutput, report_id, data + skip, (CFIndex)(len - skip));
    if (rc != kIOReturnSuccess)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = len, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result dequeue_one(Apple_Device* ad, u8* out, usize cap)
{
    if (ad->queue_lens.count == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_WOULD_BLOCK };
    usize rlen = ad->queue_lens.items[0];
    usize copy = rlen < cap ? rlen : cap;
    memcpy(out, ad->queue.items, copy);
    memmove(ad->queue.items, ad->queue.items + rlen, ad->queue.count - rlen);
    ad->queue.count -= rlen;
    memmove(ad->queue_lens.items, ad->queue_lens.items + 1, (ad->queue_lens.count - 1) * sizeof(usize));
    ad->queue_lens.count--;
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < rlen)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = st };
}

static Mel_Hid_Io_Result apple_read(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms)
{
    (void)user;
    Apple_Device* ad = channel.value;
    if (!ad || !ad->open)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };

    Mel_Hid_Io_Result r = dequeue_one(ad, out, cap);
    if (!mel_hid_would_block(r.status))
        return r;
    if (timeout_ms == MEL_HID_TIMEOUT_POLL)
        return r;

    CFTimeInterval slice = 0.005;
    CFTimeInterval budget = timeout_ms == MEL_HID_TIMEOUT_BLOCK ? 1.0e9 : (CFTimeInterval)timeout_ms / 1000.0;
    CFTimeInterval spent = 0.0;
    while (spent < budget)
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, slice, true);
        r = dequeue_one(ad, out, cap);
        if (!mel_hid_would_block(r.status))
            return r;
        spent += slice;
    }
    return (Mel_Hid_Io_Result){ .status = MEL_HID_TIMED_OUT | MEL_HID_WARNED };
}

static Mel_Hid_Io_Result apple_get_feature(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap)
{
    (void)user;
    Apple_Device* ad = channel.value;
    if (!ad || !ad->open)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    CFIndex  len = (CFIndex)cap;
    IOReturn rc = IOHIDDeviceGetReport(ad->device, kIOHIDReportTypeFeature, report_id, out, &len);
    if (rc != kIOReturnSuccess)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = (usize)len, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result apple_send_feature(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    Apple_Device* ad = channel.value;
    if (!ad || !ad->open || len == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    u8       report_id = data[0];
    usize    skip = report_id != 0 ? 1 : 0;
    IOReturn rc = IOHIDDeviceSetReport(ad->device, kIOHIDReportTypeFeature, report_id, data + skip, (CFIndex)(len - skip));
    if (rc != kIOReturnSuccess)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = len, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result apple_get_report_descriptor(void* user, Mel_Hid_Channel channel, u8* out, usize cap)
{
    (void)user;
    Apple_Device* ad = channel.value;
    if (!ad)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    CFTypeRef ref = IOHIDDeviceGetProperty(ad->device, CFSTR(kIOHIDReportDescriptorKey));
    if (!ref || CFGetTypeID(ref) != CFDataGetTypeID())
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    CFDataRef data = (CFDataRef)ref;
    CFIndex   total = CFDataGetLength(data);
    usize     copy = (usize)total < cap ? (usize)total : cap;
    memcpy(out, CFDataGetBytePtr(data), copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < (usize)total)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = (usize)total, .status = st };
}

static Mel_Hid_Io_Result apple_get_string(void* user, Mel_Hid_Channel channel, u8 string_index, u8* out, usize cap)
{
    (void)user;
    (void)string_index;
    Apple_Device* ad = channel.value;
    if (!ad)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    char buf[MEL_HID_STRING_CAP];
    cf_string_copy(ad->device, CFSTR(kIOHIDProductKey), buf, sizeof buf);
    usize len = strlen(buf);
    usize copy = len < cap ? len : cap;
    memcpy(out, buf, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < len)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = st };
}

static void* apple_native(void* user, Mel_Hid_Channel channel)
{
    (void)user;
    Apple_Device* ad = channel.value;
    return ad ? (void*)ad->device : NULL;
}

void mel_hid__register_host_providers(const Mel_Alloc* alloc)
{
    g_apple.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_apple.devices, g_apple.alloc);
    g_apple.manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (g_apple.manager)
    {
        IOHIDManagerSetDeviceMatching(g_apple.manager, NULL);
        IOHIDManagerOpen(g_apple.manager, kIOHIDOptionsTypeNone);
    }

    Mel_Hid_Provider_Desc desc = {
        .name = "iohid",
        .user = &g_apple,
        .enumerate = apple_enumerate,
        .open = apple_open,
        .close = apple_close,
        .write = apple_write,
        .read = apple_read,
        .get_feature = apple_get_feature,
        .send_feature = apple_send_feature,
        .get_report_descriptor = apple_get_report_descriptor,
        .get_string = apple_get_string,
        .native = apple_native,
    };
    mel_hid_provider_register(&desc);
}

IOHIDDeviceRef mel_hid_macos_device(Mel_Hid_Device d) { return (IOHIDDeviceRef)mel_hid_native(d); }
