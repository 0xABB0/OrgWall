#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>

#include <hid/hid.h>
#include <hid/provider.h>
#include <hid/win32/win32.h>

#include "../../src/hid_internal.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct
{
    u64     stable_id;
    wchar_t path[512];
    HANDLE  handle;
    bool    open;
} Win32_Device;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Win32_Device*) devices;
} Win32_Backend;

static Win32_Backend g_win32;

static u64 fnv1a_wide(const wchar_t* s)
{
    u64 h = 1469598103934665603ull;
    for (; *s; s++)
    {
        h ^= (u64)(u16)*s;
        h *= 1099511628211ull;
    }
    return h;
}

static void wide_to_utf8(const wchar_t* in, char* out, usize cap)
{
    out[0] = '\0';
    if (!in)
        return;
    WideCharToMultiByte(CP_UTF8, 0, in, -1, out, (int)cap, NULL, NULL);
    out[cap - 1] = '\0';
}

static Win32_Device* find_device(u64 stable_id)
{
    for (usize i = 0; i < g_win32.devices.count; i++)
        if (g_win32.devices.items[i]->stable_id == stable_id)
            return g_win32.devices.items[i];
    return NULL;
}

static Mel_Hid_Bus bus_from_path(const wchar_t* path)
{
    if (wcsstr(path, L"_VID&") || wcsstr(path, L"\\\\?\\hid#vid"))
        return MEL_HID_BUS_USB;
    if (wcsstr(path, L"{00001124") || wcsstr(path, L"_BTHHFENUM") || wcsstr(path, L"bthhid"))
        return MEL_HID_BUS_BLUETOOTH;
    return MEL_HID_BUS_UNKNOWN;
}

static void fill_descriptor(HANDLE h, const wchar_t* path, u64 stable_id, Mel_Hid_Descriptor* d)
{
    memset(d, 0, sizeof *d);
    wide_to_utf8(path, d->path, MEL_HID_STRING_CAP);
    d->bus = bus_from_path(path);

    HIDD_ATTRIBUTES attrs;
    attrs.Size = sizeof attrs;
    if (HidD_GetAttributes(h, &attrs))
    {
        d->vendor_id = attrs.VendorID;
        d->product_id = attrs.ProductID;
        d->version_bcd = attrs.VersionNumber;
    }

    PHIDP_PREPARSED_DATA pp = NULL;
    if (HidD_GetPreparsedData(h, &pp))
    {
        HIDP_CAPS caps;
        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS)
        {
            d->usage_page = caps.UsagePage;
            d->usage = caps.Usage;
            d->input_report_len = caps.InputReportByteLength;
            d->output_report_len = caps.OutputReportByteLength;
            d->feature_report_len = caps.FeatureReportByteLength;
        }
        HidD_FreePreparsedData(pp);
    }

    wchar_t buf[MEL_HID_STRING_CAP];
    if (HidD_GetManufacturerString(h, buf, sizeof buf))
        wide_to_utf8(buf, d->manufacturer, MEL_HID_STRING_CAP);
    if (HidD_GetProductString(h, buf, sizeof buf))
        wide_to_utf8(buf, d->product, MEL_HID_STRING_CAP);
    if (HidD_GetSerialNumberString(h, buf, sizeof buf))
        wide_to_utf8(buf, d->serial, MEL_HID_STRING_CAP);

    (void)stable_id;
}

static u32 win32_enumerate(void* user, Mel_Hid_Raw* out, u32 cap)
{
    Win32_Backend* be = user;
    GUID           guid;
    HidD_GetHidGuid(&guid);

    HDEVINFO info = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE)
        return 0;

    u32                      written = 0;
    SP_DEVICE_INTERFACE_DATA iface;
    iface.cbSize = sizeof iface;
    for (DWORD i = 0; written < cap && SetupDiEnumDeviceInterfaces(info, NULL, &guid, i, &iface); i++)
    {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &iface, NULL, 0, &required, NULL);
        if (required == 0)
            continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)mel_alloc(be->alloc, required);
        if (!detail)
            continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, NULL, NULL))
        {
            mel_dealloc(be->alloc, detail);
            continue;
        }

        HANDLE probe = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (probe == INVALID_HANDLE_VALUE)
            probe = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

        u64           id = fnv1a_wide(detail->DevicePath);
        Win32_Device* wd = find_device(id);
        if (!wd)
        {
            wd = mel_alloc_type(be->alloc, Win32_Device);
            memset(wd, 0, sizeof *wd);
            wd->stable_id = id;
            wd->handle = INVALID_HANDLE_VALUE;
            wcsncpy(wd->path, detail->DevicePath, 511);
            mel_array_push(&be->devices, wd);
        }

        Mel_Hid_Raw* r = &out[written++];
        r->stable_id = id;
        if (probe != INVALID_HANDLE_VALUE)
        {
            fill_descriptor(probe, detail->DevicePath, id, &r->desc);
            CloseHandle(probe);
        }
        else
        {
            memset(&r->desc, 0, sizeof r->desc);
            wide_to_utf8(detail->DevicePath, r->desc.path, MEL_HID_STRING_CAP);
            r->desc.bus = bus_from_path(detail->DevicePath);
        }

        mel_dealloc(be->alloc, detail);
    }

    SetupDiDestroyDeviceInfoList(info);
    return written;
}

static Mel_Hid_Status win32_open(void* user, u64 stable_id, Mel_Hid_Channel* out_channel)
{
    (void)user;
    Win32_Device* wd = find_device(stable_id);
    if (!wd)
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    HANDLE h = CreateFileW(wd->path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        return MEL_HID_ERROR | (e == ERROR_ACCESS_DENIED ? MEL_HID_ACCESS_DENIED : 0u);
    }
    wd->handle = h;
    wd->open = true;
    *out_channel = (Mel_Hid_Channel){ .value = wd, .fd = MEL_HID_NO_FD, .bus = bus_from_path(wd->path) };
    return MEL_HID_OK;
}

static void win32_close(void* user, u64 stable_id, Mel_Hid_Channel channel)
{
    (void)user;
    (void)stable_id;
    Win32_Device* wd = channel.value;
    if (!wd || !wd->open)
        return;
    CloseHandle(wd->handle);
    wd->handle = INVALID_HANDLE_VALUE;
    wd->open = false;
}

static Mel_Hid_Io_Result sync_overlapped(HANDLE h, BOOL ok, OVERLAPPED* ov, DWORD timeout_ms)
{
    if (!ok && GetLastError() != ERROR_IO_PENDING)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    DWORD wait = WaitForSingleObject(ov->hEvent, timeout_ms);
    if (wait == WAIT_TIMEOUT)
    {
        CancelIo(h);
        return (Mel_Hid_Io_Result){ .status = MEL_HID_TIMED_OUT | MEL_HID_WARNED };
    }
    DWORD transferred = 0;
    if (!GetOverlappedResult(h, ov, &transferred, TRUE))
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = transferred, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result win32_write(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    Win32_Device* wd = channel.value;
    if (!wd || !wd->open)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL              ok = WriteFile(wd->handle, data, (DWORD)len, NULL, &ov);
    Mel_Hid_Io_Result r = sync_overlapped(wd->handle, ok, &ov, INFINITE);
    CloseHandle(ov.hEvent);
    return r;
}

static Mel_Hid_Io_Result win32_read(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms)
{
    (void)user;
    Win32_Device* wd = channel.value;
    if (!wd || !wd->open)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD wait_ms = timeout_ms == MEL_HID_TIMEOUT_BLOCK ? INFINITE : (DWORD)timeout_ms;
    BOOL  ok = ReadFile(wd->handle, out, (DWORD)cap, NULL, &ov);
    if (timeout_ms == MEL_HID_TIMEOUT_POLL)
    {
        DWORD transferred = 0;
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(ov.hEvent, 0) == WAIT_TIMEOUT)
            {
                CancelIo(wd->handle);
                CloseHandle(ov.hEvent);
                return (Mel_Hid_Io_Result){ .status = MEL_HID_WOULD_BLOCK };
            }
        }
        GetOverlappedResult(wd->handle, &ov, &transferred, TRUE);
        CloseHandle(ov.hEvent);
        return (Mel_Hid_Io_Result){ .bytes = transferred, .status = MEL_HID_OK };
    }
    Mel_Hid_Io_Result r = sync_overlapped(wd->handle, ok, &ov, wait_ms);
    CloseHandle(ov.hEvent);
    return r;
}

static Mel_Hid_Io_Result win32_get_feature(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap)
{
    (void)user;
    Win32_Device* wd = channel.value;
    if (!wd || !wd->open || cap == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    out[0] = report_id;
    if (!HidD_GetFeature(wd->handle, out, (ULONG)cap))
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = cap, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result win32_send_feature(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    Win32_Device* wd = channel.value;
    if (!wd || !wd->open || len == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    if (!HidD_SetFeature(wd->handle, (PVOID)data, (ULONG)len))
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = len, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result win32_get_report_descriptor(void* user, Mel_Hid_Channel channel, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    (void)out;
    (void)cap;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result win32_get_string(void* user, Mel_Hid_Channel channel, u8 string_index, u8* out, usize cap)
{
    (void)user;
    Win32_Device* wd = channel.value;
    if (!wd || !wd->open)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    wchar_t buf[MEL_HID_STRING_CAP];
    if (!HidD_GetIndexedString(wd->handle, string_index, buf, sizeof buf))
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    char utf8[MEL_HID_STRING_CAP];
    wide_to_utf8(buf, utf8, MEL_HID_STRING_CAP);
    usize len = strlen(utf8);
    usize copy = len < cap ? len : cap;
    memcpy(out, utf8, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < len)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = st };
}

static void* win32_native(void* user, Mel_Hid_Channel channel)
{
    (void)user;
    Win32_Device* wd = channel.value;
    return wd ? (void*)wd->handle : NULL;
}

void mel_hid__register_host_providers(const Mel_Alloc* alloc)
{
    g_win32.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_win32.devices, g_win32.alloc);

    Mel_Hid_Provider_Desc desc = {
        .name = "hid.dll",
        .user = &g_win32,
        .enumerate = win32_enumerate,
        .open = win32_open,
        .close = win32_close,
        .write = win32_write,
        .read = win32_read,
        .get_feature = win32_get_feature,
        .send_feature = win32_send_feature,
        .get_report_descriptor = win32_get_report_descriptor,
        .get_string = win32_get_string,
        .native = win32_native,
    };
    mel_hid_provider_register(&desc);
}

HANDLE mel_hid_win32_handle(Mel_Hid_Device d)
{
    void* n = mel_hid_native(d);
    return n ? (HANDLE)n : INVALID_HANDLE_VALUE;
}
