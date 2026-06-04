#include <input/provider.h>
#include <input/linux/linux.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <log/log.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include "../input_internal.h"

#define MEL_LINUX_NAME_CAP  80
#define MEL_LINUX_KEY_WORDS ((KEY_CNT + 31) / 32)

typedef struct
{
    int  fd;
    u64  stable_id;
    char name[MEL_LINUX_NAME_CAP];
    u32  caps;
    bool has_keys;
    bool has_rel;
    bool has_abs;
} Linux_Device;

static struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Linux_Device) devices;
    bool  scanned;
    void* wl_seat;
} g_lin;

static bool bit_test(const u32* arr, u32 bit) { return (arr[bit >> 5] & (1u << (bit & 31))) != 0; }

static u64 stable_id_from_path(const char* path)
{
    u64 h = 1469598103934665603ULL;
    for (const char* p = path; *p; p++)
    {
        h ^= (u8)*p;
        h *= 1099511628211ULL;
    }
    return h | 1ULL;
}

static void scan_devices(void)
{
    if (g_lin.devices.allocator == NULL)
        mel_array_init(&g_lin.devices, g_lin.alloc ? g_lin.alloc : mel_alloc_heap());
    for (usize i = 0; i < g_lin.devices.count; i++)
        if (g_lin.devices.items[i].fd >= 0)
            close(g_lin.devices.items[i].fd);
    mel_array_clear(&g_lin.devices);

    DIR* d = opendir("/dev/input");
    if (!d)
    {
        mel_log_warn("input", "linux: /dev/input not readable; no evdev devices");
        g_lin.scanned = true;
        return;
    }

    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        char path[256];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        Linux_Device dev = { .fd = fd, .stable_id = stable_id_from_path(path) };
        if (ioctl(fd, EVIOCGNAME(sizeof dev.name), dev.name) < 0)
            snprintf(dev.name, sizeof dev.name, "%s", e->d_name);

        u32 ev_bits[(EV_CNT + 31) / 32] = { 0 };
        ioctl(fd, EVIOCGBIT(0, sizeof ev_bits), ev_bits);
        dev.has_keys = bit_test(ev_bits, EV_KEY);
        dev.has_rel = bit_test(ev_bits, EV_REL);
        dev.has_abs = bit_test(ev_bits, EV_ABS);

        u32 key_bits[MEL_LINUX_KEY_WORDS] = { 0 };
        if (dev.has_keys)
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key_bits), key_bits);

        if (dev.has_keys && bit_test(key_bits, KEY_A) && bit_test(key_bits, KEY_Z))
            dev.caps |= MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT;
        if (dev.has_rel || (dev.has_keys && bit_test(key_bits, BTN_LEFT) && !dev.has_abs))
            dev.caps |= MEL_INPUT_CAP_MOUSE | MEL_INPUT_CAP_RELATIVE;
        if (dev.has_abs && bit_test(key_bits, BTN_TOUCH))
            dev.caps |= MEL_INPUT_CAP_TOUCH | MEL_INPUT_CAP_PRESSURE;
        if (dev.has_abs && bit_test(key_bits, BTN_TOOL_PEN))
            dev.caps |= MEL_INPUT_CAP_PEN | MEL_INPUT_CAP_PRESSURE | MEL_INPUT_CAP_TILT;

        if (dev.caps == 0)
        {
            close(fd);
            continue;
        }
        mel_array_push(&g_lin.devices, dev);
    }
    closedir(d);
    g_lin.scanned = true;
}

static Linux_Device* device_by_stable(u64 sid)
{
    for (usize i = 0; i < g_lin.devices.count; i++)
        if (g_lin.devices.items[i].stable_id == sid)
            return &g_lin.devices.items[i];
    return NULL;
}

static Mel_Scancode linux_scancode_from_key(u16 key)
{
    switch (key)
    {
    case KEY_A:
        return MEL_SCANCODE_A;
    case KEY_B:
        return MEL_SCANCODE_B;
    case KEY_C:
        return MEL_SCANCODE_C;
    case KEY_D:
        return MEL_SCANCODE_D;
    case KEY_E:
        return MEL_SCANCODE_E;
    case KEY_F:
        return MEL_SCANCODE_F;
    case KEY_G:
        return MEL_SCANCODE_G;
    case KEY_H:
        return MEL_SCANCODE_H;
    case KEY_I:
        return MEL_SCANCODE_I;
    case KEY_J:
        return MEL_SCANCODE_J;
    case KEY_K:
        return MEL_SCANCODE_K;
    case KEY_L:
        return MEL_SCANCODE_L;
    case KEY_M:
        return MEL_SCANCODE_M;
    case KEY_N:
        return MEL_SCANCODE_N;
    case KEY_O:
        return MEL_SCANCODE_O;
    case KEY_P:
        return MEL_SCANCODE_P;
    case KEY_Q:
        return MEL_SCANCODE_Q;
    case KEY_R:
        return MEL_SCANCODE_R;
    case KEY_S:
        return MEL_SCANCODE_S;
    case KEY_T:
        return MEL_SCANCODE_T;
    case KEY_U:
        return MEL_SCANCODE_U;
    case KEY_V:
        return MEL_SCANCODE_V;
    case KEY_W:
        return MEL_SCANCODE_W;
    case KEY_X:
        return MEL_SCANCODE_X;
    case KEY_Y:
        return MEL_SCANCODE_Y;
    case KEY_Z:
        return MEL_SCANCODE_Z;
    case KEY_1:
        return MEL_SCANCODE_1;
    case KEY_2:
        return MEL_SCANCODE_2;
    case KEY_3:
        return MEL_SCANCODE_3;
    case KEY_4:
        return MEL_SCANCODE_4;
    case KEY_5:
        return MEL_SCANCODE_5;
    case KEY_6:
        return MEL_SCANCODE_6;
    case KEY_7:
        return MEL_SCANCODE_7;
    case KEY_8:
        return MEL_SCANCODE_8;
    case KEY_9:
        return MEL_SCANCODE_9;
    case KEY_0:
        return MEL_SCANCODE_0;
    case KEY_ENTER:
        return MEL_SCANCODE_RETURN;
    case KEY_ESC:
        return MEL_SCANCODE_ESCAPE;
    case KEY_BACKSPACE:
        return MEL_SCANCODE_BACKSPACE;
    case KEY_TAB:
        return MEL_SCANCODE_TAB;
    case KEY_SPACE:
        return MEL_SCANCODE_SPACE;
    case KEY_LEFTSHIFT:
        return MEL_SCANCODE_LSHIFT;
    case KEY_RIGHTSHIFT:
        return MEL_SCANCODE_RSHIFT;
    case KEY_LEFTCTRL:
        return MEL_SCANCODE_LCTRL;
    case KEY_RIGHTCTRL:
        return MEL_SCANCODE_RCTRL;
    case KEY_LEFTALT:
        return MEL_SCANCODE_LALT;
    case KEY_RIGHTALT:
        return MEL_SCANCODE_RALT;
    case KEY_LEFTMETA:
        return MEL_SCANCODE_LGUI;
    case KEY_RIGHTMETA:
        return MEL_SCANCODE_RGUI;
    case KEY_LEFT:
        return MEL_SCANCODE_LEFT;
    case KEY_RIGHT:
        return MEL_SCANCODE_RIGHT;
    case KEY_UP:
        return MEL_SCANCODE_UP;
    case KEY_DOWN:
        return MEL_SCANCODE_DOWN;
    case KEY_CAPSLOCK:
        return MEL_SCANCODE_CAPSLOCK;
    default:
        return MEL_SCANCODE_UNKNOWN;
    }
}

static u32 linux_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (!g_lin.scanned)
        scan_devices();
    u32 n = 0;
    for (usize i = 0; i < g_lin.devices.count && n < cap; i++)
    {
        Linux_Device* dev = &g_lin.devices.items[i];
        out[n++] = (Mel_Input_Raw){
            .stable_id = dev->stable_id,
            .desc = { .name = str8_from_cstr(dev->name), .caps = dev->caps, .pressure_max = (dev->caps & MEL_INPUT_CAP_PRESSURE) ? 1.0f : 0.0f },
        };
    }
    return n;
}

static void linux_pump(void* user, Mel_Input_Sink* sink)
{
    (void)user;
    if (sink == NULL)
        return;
    for (usize i = 0; i < g_lin.devices.count; i++)
    {
        Linux_Device*      dev = &g_lin.devices.items[i];
        struct input_event evt;
        while (read(dev->fd, &evt, sizeof evt) == (ssize_t)sizeof evt)
        {
            if (evt.type == EV_KEY)
            {
                if (evt.code == BTN_LEFT || evt.code == BTN_RIGHT || evt.code == BTN_MIDDLE)
                {
                    u32                   mask = evt.code == BTN_LEFT ? MEL_INPUT_MOUSE_BUTTON_LEFT : (evt.code == BTN_RIGHT ? MEL_INPUT_MOUSE_BUTTON_RIGHT : MEL_INPUT_MOUSE_BUTTON_MIDDLE);
                    Mel_Input_Mouse_Event me = { .button_changed = mask, .button_down = evt.value != 0, .buttons = evt.value ? mask : 0 };
                    mel_input_sink_mouse(sink, dev->stable_id, &me);
                }
                else
                {
                    Mel_Input_Key_Event ke = { .scancode = linux_scancode_from_key((u16)evt.code), .down = evt.value != 0, .repeat = evt.value == 2 };
                    mel_input_sink_key(sink, dev->stable_id, &ke);
                }
            }
            else if (evt.type == EV_REL)
            {
                Mel_Input_Mouse_Event me = { 0 };
                if (evt.code == REL_X)
                    me.dx = (f32)evt.value;
                else if (evt.code == REL_Y)
                    me.dy = (f32)evt.value;
                else if (evt.code == REL_WHEEL)
                    me.wheel_y = (f32)evt.value;
                else if (evt.code == REL_HWHEEL)
                    me.wheel_x = (f32)evt.value;
                else
                    continue;
                mel_input_sink_mouse(sink, dev->stable_id, &me);
            }
        }
    }
}

static int linux_fd_for_stable(u64 sid)
{
    Linux_Device* dev = device_by_stable(sid);
    return dev ? dev->fd : -1;
}

static void* linux_native(void* user, u64 sid)
{
    (void)user;
    Linux_Device* dev = device_by_stable(sid);
    return dev ? (void*)(usize)dev->fd : NULL;
}

static Mel_Input_Status linux_text_start(void* user, const Mel_Input_Text_Opt* opt)
{
    (void)user;
    (void)opt;
    if (g_lin.wl_seat == NULL)
        return MEL_INPUT_WARNED | MEL_INPUT_DEGRADED;
    return MEL_INPUT_OK;
}

static void linux_text_stop(void* user) { (void)user; }

static Mel_Input_Provider_Desc g_desc;

void mel_input__register_host_providers(void)
{
    g_lin.alloc = mel_alloc_heap();
    g_desc = (Mel_Input_Provider_Desc){
        .name = "linux-evdev",
        .enumerate = linux_enumerate,
        .pump = linux_pump,
        .text_start = linux_text_start,
        .text_stop = linux_text_stop,
        .native = linux_native,
    };
    mel_input_provider_register(&g_desc);
}

int mel_input_linux_fd(Mel_Input_Device d)
{
    u64 sid = 0;
    if (!mel_input__stable_id(d, &sid))
        return -1;
    return linux_fd_for_stable(sid);
}

void mel_input_linux_set_wayland(void* wl_seat) { g_lin.wl_seat = wl_seat; }
