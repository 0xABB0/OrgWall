#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>

#include <hid/hid.h>
#include <hid/provider.h>
#include <hid/linux/linux.h>

#include "../../src/hid_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libudev.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>

typedef struct
{
    u64  stable_id;
    char node[MEL_HID_STRING_CAP];
    int  fd;
    bool open;
} Linux_Device;

typedef struct
{
    const Mel_Alloc* alloc;
    struct udev*     udev;
    Mel_Array(Linux_Device*) devices;
} Linux_Backend;

static Linux_Backend g_linux;

static u64 fnv1a(const char* s)
{
    u64 h = 1469598103934665603ull;
    for (; *s; s++)
    {
        h ^= (u64)(u8)*s;
        h *= 1099511628211ull;
    }
    return h;
}

static Mel_Hid_Bus bus_from_id(u32 bus_type)
{
    switch (bus_type)
    {
    case BUS_USB:
        return MEL_HID_BUS_USB;
    case BUS_BLUETOOTH:
        return MEL_HID_BUS_BLUETOOTH;
    case BUS_I2C:
        return MEL_HID_BUS_I2C;
    case BUS_SPI:
        return MEL_HID_BUS_SPI;
    default:
        return MEL_HID_BUS_UNKNOWN;
    }
}

static void copy_attr(struct udev_device* parent, const char* attr, char* out, usize cap)
{
    out[0] = '\0';
    if (!parent)
        return;
    const char* v = udev_device_get_sysattr_value(parent, attr);
    if (v)
    {
        strncpy(out, v, cap - 1);
        out[cap - 1] = '\0';
    }
}

static Linux_Device* find_device(u64 stable_id)
{
    for (usize i = 0; i < g_linux.devices.count; i++)
        if (g_linux.devices.items[i]->stable_id == stable_id)
            return g_linux.devices.items[i];
    return NULL;
}

static void fill_from_fd(int fd, Mel_Hid_Descriptor* d)
{
    struct hidraw_devinfo info;
    memset(&info, 0, sizeof info);
    if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0)
    {
        d->bus = bus_from_id((u32)info.bustype);
        d->vendor_id = (u16)info.vendor;
        d->product_id = (u16)info.product;
    }

    int desc_size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) >= 0 && desc_size > 0)
    {
        struct hidraw_report_descriptor rpt;
        memset(&rpt, 0, sizeof rpt);
        rpt.size = (u32)desc_size;
        if (ioctl(fd, HIDIOCGRDESC, &rpt) >= 0)
        {
            for (u32 i = 0; i + 2 < rpt.size; i++)
            {
                if (rpt.value[i] == 0x05 && d->usage_page == 0)
                    d->usage_page = rpt.value[i + 1];
                if (rpt.value[i] == 0x09 && d->usage == 0)
                    d->usage = rpt.value[i + 1];
            }
        }
    }

    char name[MEL_HID_STRING_CAP];
    if (ioctl(fd, HIDIOCGRAWNAME(MEL_HID_STRING_CAP), name) >= 0)
    {
        name[MEL_HID_STRING_CAP - 1] = '\0';
        strncpy(d->product, name, MEL_HID_STRING_CAP - 1);
        d->product[MEL_HID_STRING_CAP - 1] = '\0';
    }
    char phys[MEL_HID_STRING_CAP];
    if (ioctl(fd, HIDIOCGRAWPHYS(MEL_HID_STRING_CAP), phys) >= 0)
    {
        phys[MEL_HID_STRING_CAP - 1] = '\0';
    }
}

static u32 linux_enumerate(void* user, Mel_Hid_Raw* out, u32 cap)
{
    Linux_Backend* be = user;
    if (!be->udev)
        return 0;
    struct udev_enumerate* en = udev_enumerate_new(be->udev);
    if (!en)
        return 0;
    udev_enumerate_add_match_subsystem(en, "hidraw");
    udev_enumerate_scan_devices(en);

    u32                     written = 0;
    struct udev_list_entry* entry;
    struct udev_list_entry* list = udev_enumerate_get_list_entry(en);
    udev_list_entry_foreach(entry, list)
    {
        if (written >= cap)
            break;
        const char*         path = udev_list_entry_get_name(entry);
        struct udev_device* dev = udev_device_new_from_syspath(be->udev, path);
        if (!dev)
            continue;
        const char* node = udev_device_get_devnode(dev);
        if (!node)
        {
            udev_device_unref(dev);
            continue;
        }

        u64           id = fnv1a(node);
        Linux_Device* ld = find_device(id);
        if (!ld)
        {
            ld = mel_alloc_type(be->alloc, Linux_Device);
            memset(ld, 0, sizeof *ld);
            ld->stable_id = id;
            ld->fd = MEL_HID_NO_FD;
            strncpy(ld->node, node, MEL_HID_STRING_CAP - 1);
            mel_array_push(&be->devices, ld);
        }

        Mel_Hid_Raw* r = &out[written++];
        memset(r, 0, sizeof *r);
        r->stable_id = id;
        Mel_Hid_Descriptor* d = &r->desc;
        strncpy(d->path, node, MEL_HID_STRING_CAP - 1);

        struct udev_device* usb = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
        copy_attr(usb, "manufacturer", d->manufacturer, MEL_HID_STRING_CAP);
        copy_attr(usb, "product", d->product, MEL_HID_STRING_CAP);
        copy_attr(usb, "serial", d->serial, MEL_HID_STRING_CAP);
        if (usb)
        {
            const char* ver = udev_device_get_sysattr_value(usb, "bcdDevice");
            if (ver)
                d->version_bcd = (u16)strtoul(ver, NULL, 16);
        }

        int probe = open(node, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (probe >= 0)
        {
            fill_from_fd(probe, d);
            close(probe);
        }

        udev_device_unref(dev);
    }

    udev_enumerate_unref(en);
    return written;
}

static Mel_Hid_Status linux_open(void* user, u64 stable_id, Mel_Hid_Channel* out_channel)
{
    (void)user;
    Linux_Device* ld = find_device(stable_id);
    if (!ld)
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    int fd = open(ld->node, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
    {
        if (errno == EACCES || errno == EPERM)
            return MEL_HID_ERROR | MEL_HID_ACCESS_DENIED;
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    }
    ld->fd = fd;
    ld->open = true;
    *out_channel = (Mel_Hid_Channel){ .value = ld, .fd = fd, .bus = MEL_HID_BUS_UNKNOWN };
    return MEL_HID_OK;
}

static void linux_close(void* user, u64 stable_id, Mel_Hid_Channel channel)
{
    (void)user;
    (void)stable_id;
    Linux_Device* ld = channel.value;
    if (!ld || !ld->open)
        return;
    close(ld->fd);
    ld->fd = MEL_HID_NO_FD;
    ld->open = false;
}

static Mel_Hid_Io_Result linux_write(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    if (channel.fd < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    ssize_t w = write(channel.fd, data, len);
    if (w < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = (usize)w, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result linux_read(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms)
{
    (void)user;
    if (channel.fd < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };

    if (timeout_ms != MEL_HID_TIMEOUT_POLL)
    {
        struct pollfd pfd = { .fd = channel.fd, .events = POLLIN };
        int           rc = poll(&pfd, 1, timeout_ms);
        if (rc == 0)
            return (Mel_Hid_Io_Result){ .status = MEL_HID_TIMED_OUT | MEL_HID_WARNED };
        if (rc < 0)
            return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    }

    ssize_t r = read(channel.fd, out, cap);
    if (r < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return (Mel_Hid_Io_Result){ .status = MEL_HID_WOULD_BLOCK };
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    }
    return (Mel_Hid_Io_Result){ .bytes = (usize)r, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result linux_get_feature(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap)
{
    (void)user;
    if (channel.fd < 0 || cap == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    out[0] = report_id;
    int rc = ioctl(channel.fd, HIDIOCGFEATURE(cap), out);
    if (rc < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = (usize)rc, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result linux_send_feature(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    if (channel.fd < 0 || len == 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    int rc = ioctl(channel.fd, HIDIOCSFEATURE(len), data);
    if (rc < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    return (Mel_Hid_Io_Result){ .bytes = (usize)rc, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result linux_get_report_descriptor(void* user, Mel_Hid_Channel channel, u8* out, usize cap)
{
    (void)user;
    if (channel.fd < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    int desc_size = 0;
    if (ioctl(channel.fd, HIDIOCGRDESCSIZE, &desc_size) < 0 || desc_size <= 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    struct hidraw_report_descriptor rpt;
    memset(&rpt, 0, sizeof rpt);
    rpt.size = (u32)desc_size;
    if (ioctl(channel.fd, HIDIOCGRDESC, &rpt) < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_DEVICE_LOST };
    usize copy = (usize)desc_size < cap ? (usize)desc_size : cap;
    memcpy(out, rpt.value, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < (usize)desc_size)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = (usize)desc_size, .status = st };
}

static Mel_Hid_Io_Result linux_get_string(void* user, Mel_Hid_Channel channel, u8 string_index, u8* out, usize cap)
{
    (void)user;
    (void)string_index;
    Linux_Device* ld = channel.value;
    if (!ld || channel.fd < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_NOT_OPEN };
    char name[MEL_HID_STRING_CAP];
    if (ioctl(channel.fd, HIDIOCGRAWNAME(MEL_HID_STRING_CAP), name) < 0)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR | MEL_HID_UNSUPPORTED };
    name[MEL_HID_STRING_CAP - 1] = '\0';
    usize len = strlen(name);
    usize copy = len < cap ? len : cap;
    memcpy(out, name, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < len)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = st };
}

static void* linux_native(void* user, Mel_Hid_Channel channel)
{
    (void)user;
    Linux_Device* ld = channel.value;
    return ld ? (void*)(intptr_t)ld->fd : NULL;
}

void mel_hid__register_host_providers(const Mel_Alloc* alloc)
{
    g_linux.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_linux.devices, g_linux.alloc);
    g_linux.udev = udev_new();
    if (!g_linux.udev)
        return;

    Mel_Hid_Provider_Desc desc = {
        .name = "hidraw",
        .user = &g_linux,
        .enumerate = linux_enumerate,
        .open = linux_open,
        .close = linux_close,
        .write = linux_write,
        .read = linux_read,
        .get_feature = linux_get_feature,
        .send_feature = linux_send_feature,
        .get_report_descriptor = linux_get_report_descriptor,
        .get_string = linux_get_string,
        .native = linux_native,
    };
    mel_hid_provider_register(&desc);
}

int mel_hid_linux_fd(Mel_Hid_Device d)
{
    void* n = mel_hid_native(d);
    return n ? (int)(intptr_t)n : MEL_HID_NO_FD;
}
