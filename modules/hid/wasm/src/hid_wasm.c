#include <emscripten.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>

#include <hid/hid.h>
#include <hid/provider.h>
#include <hid/wasm/wasm.h>

#include "../../src/hid_internal.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    const Mel_Alloc* alloc;
    bool             available;
} Wasm_Backend;

static Wasm_Backend g_wasm;

EM_JS(int, mel_hid_js_available, (void), { return (navigator && navigator.hid) ? 1 : 0; });

EM_JS(int, mel_hid_js_refresh, (void), {
    if (!(navigator && navigator.hid))
        return 0;
    var t = Module.__mel_hid;
    if (!t)
    {
        t = Module.__mel_hid = { devices : [], queues : {}, index : new Map() };
        t.adopt = function(dev) {
            if (t.index.has(dev))
                return t.index.get(dev);
            var i = t.devices.length;
            t.devices.push(dev);
            t.index.set(dev, i);
            return i;
        };
        t.forget = function(dev) {
            if (!t.index.has(dev))
                return;
            var i = t.index.get(dev);
            t.devices[i] = null;
            t.index.delete(dev);
            t.queues[i] = [];
        };
        navigator.hid.addEventListener("connect", function(e) { t.adopt(e.device); });
        navigator.hid.addEventListener("disconnect", function(e) { t.forget(e.device); });
        navigator.hid.getDevices().then(function(list) {
            for (var i = 0; i < list.length; i++)
                t.adopt(list[i]);
        });
    }
    return t.devices.length;
});

EM_JS(int, mel_hid_js_request, (int vendor_id, int product_id), {
    if (!(navigator && navigator.hid))
        return -1;
    var t = Module.__mel_hid;
    if (!t)
        return -1;
    var filters = [];
    if (vendor_id || product_id)
    {
        var f = {};
        if (vendor_id)
            f.vendorId = vendor_id;
        if (product_id)
            f.productId = product_id;
        filters.push(f);
    }
    navigator.hid.requestDevice({ filters : filters }).then(function(list) {
        for (var i = 0; i < list.length; i++)
            t.adopt(list[i]);
    }).catch(function(e) {});
    return 0;
});

EM_JS(int, mel_hid_js_count, (void), { return (Module.__mel_hid && Module.__mel_hid.devices) ? Module.__mel_hid.devices.length : 0; });

EM_JS(int, mel_hid_js_present, (int idx), {
    var t = Module.__mel_hid;
    return (t && t.devices[idx]) ? 1 : 0;
});

EM_JS(int, mel_hid_js_vid, (int idx), {
    var t = Module.__mel_hid;
    return (t && t.devices[idx]) ? (t.devices[idx].vendorId | 0) : 0;
});

EM_JS(int, mel_hid_js_pid, (int idx), {
    var t = Module.__mel_hid;
    return (t && t.devices[idx]) ? (t.devices[idx].productId | 0) : 0;
});

EM_JS(void, mel_hid_js_product, (int idx, char* out, int cap), {
    var t = Module.__mel_hid;
    var name = (t && t.devices[idx]) ? (t.devices[idx].productName || "") : "";
    stringToUTF8(name, out, cap);
});

EM_JS(int, mel_hid_js_open, (int idx), {
    var t = Module.__mel_hid;
    if (!t || !t.devices[idx])
        return -1;
    var dev = t.devices[idx];
    if (!t.queues[idx])
        t.queues[idx] = [];
    dev.oninputreport = function(e)
    {
        var buf = new Uint8Array(e.data.buffer);
        t.queues[idx].push(Array.from(buf));
    };
    dev.open();
    return idx;
});

EM_JS(void, mel_hid_js_close, (int idx), {
    var t = Module.__mel_hid;
    if (t && t.devices[idx])
    {
        try { t.devices[idx].close(); }
        catch(e) {}
        t.queues[idx] = [];
    }
});

EM_JS(int, mel_hid_js_drain, (int idx, char* out, int cap), {
    var t = Module.__mel_hid;
    if (!t || !t.queues[idx] || t.queues[idx].length === 0)
        return -1;
    var report = t.queues[idx].shift();
    var n = Math.min(report.length, cap);
    for (var i = 0; i < n; i++)
        HEAPU8[out + i] = report[i];
    return n;
});

EM_JS(int, mel_hid_js_send_output, (int idx, int report_id, char* data, int len), {
    var t = Module.__mel_hid;
    if (!t || !t.devices[idx])
        return -1;
    var buf = new Uint8Array(len);
    for (var i = 0; i < len; i++)
        buf[i] = HEAPU8[data + i];
    t.devices[idx].sendReport(report_id, buf);
    return len;
});

static u32 wasm_enumerate(void* user, Mel_Hid_Raw* out, u32 cap)
{
    Wasm_Backend* be = user;
    if (!be->available)
        return 0;
    mel_hid_js_refresh();
    int n = mel_hid_js_count();
    u32 written = 0;
    for (int i = 0; i < n && written < (int)cap; i++)
    {
        if (!mel_hid_js_present(i))
            continue;
        Mel_Hid_Raw* r = &out[written++];
        memset(r, 0, sizeof *r);
        r->stable_id = (u64)(u32)i;
        r->desc.vendor_id = (u16)mel_hid_js_vid(i);
        r->desc.product_id = (u16)mel_hid_js_pid(i);
        r->desc.bus = MEL_HID_BUS_UNKNOWN;
        mel_hid_js_product(i, r->desc.product, MEL_HID_STRING_CAP);
        snprintf(r->desc.path, MEL_HID_STRING_CAP, "webhid:%d", i);
    }
    return written;
}

static Mel_Hid_Status wasm_open(void* user, u64 stable_id, Mel_Hid_Channel* out_channel)
{
    Wasm_Backend* be = user;
    if (!be->available)
        return MEL_HID_ERROR | MEL_HID_NO_BACKEND;
    int idx = (int)(u32)stable_id;
    int rc = mel_hid_js_open(idx);
    if (rc < 0)
        return MEL_HID_ERROR | MEL_HID_ACCESS_DENIED;
    *out_channel = (Mel_Hid_Channel){ .value = (void*)(intptr_t)idx, .fd = MEL_HID_NO_FD, .bus = MEL_HID_BUS_UNKNOWN };
    return MEL_HID_OK;
}

static void wasm_close(void* user, u64 stable_id, Mel_Hid_Channel channel)
{
    (void)user;
    (void)stable_id;
    mel_hid_js_close((int)(intptr_t)channel.value);
}

static Mel_Hid_Io_Result wasm_write(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    if (len == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR };
    int idx = (int)(intptr_t)channel.value;
    u8  report_id = data[0];
    int rc = mel_hid_js_send_output(idx, report_id, (char*)(data + (report_id != 0 ? 1 : 0)), (int)(len - (report_id != 0 ? 1 : 0)));
    if (rc < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = len, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result wasm_read(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms)
{
    (void)user;
    (void)timeout_ms;
    int rc = mel_hid_js_drain((int)(intptr_t)channel.value, (char*)out, (int)cap);
    if (rc < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_WOULD_BLOCK };
    return (Mel_Hid_Io_Result){ .bytes = (usize)rc, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result wasm_get_feature(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    (void)report_id;
    (void)out;
    (void)cap;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result wasm_send_feature(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    (void)channel;
    (void)data;
    (void)len;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result wasm_get_report_descriptor(void* user, Mel_Hid_Channel channel, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    (void)out;
    (void)cap;
    return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
}

static Mel_Hid_Io_Result wasm_get_string(void* user, Mel_Hid_Channel channel, u8 string_index, u8* out, usize cap)
{
    (void)user;
    (void)string_index;
    int  idx = (int)(intptr_t)channel.value;
    char buf[MEL_HID_STRING_CAP];
    mel_hid_js_product(idx, buf, MEL_HID_STRING_CAP);
    usize len = strlen(buf);
    usize copy = len < cap ? len : cap;
    memcpy(out, buf, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < len)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = st };
}

void mel_hid__register_host_providers(const Mel_Alloc* alloc)
{
    g_wasm.alloc = alloc ? alloc : mel_alloc_heap();
    g_wasm.available = mel_hid_js_available() != 0;
    if (!g_wasm.available)
        return;

    Mel_Hid_Provider_Desc desc = {
        .name = "webhid",
        .user = &g_wasm,
        .enumerate = wasm_enumerate,
        .open = wasm_open,
        .close = wasm_close,
        .write = wasm_write,
        .read = wasm_read,
        .get_feature = wasm_get_feature,
        .send_feature = wasm_send_feature,
        .get_report_descriptor = wasm_get_report_descriptor,
        .get_string = wasm_get_string,
        .native = NULL,
    };
    mel_hid_provider_register(&desc);
}

int mel_hid_wasm_device_id(Mel_Hid_Device d)
{
    Mel_Hid_Channel ch;
    if (!mel_hid__channel(d, &ch, NULL, NULL))
        return -1;
    return (int)(intptr_t)ch.value;
}

void mel_hid_wasm_request_devices(u16 vendor_id, u16 product_id)
{
    if (!g_wasm.available)
        return;
    mel_hid_js_refresh();
    mel_hid_js_request((int)vendor_id, (int)product_id);
}
