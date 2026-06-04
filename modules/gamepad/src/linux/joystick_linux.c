#include <gamepad/provider.h>
#include <gamepad/linux/linux.h>

#include "../joystick_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(long) * 8)
#endif
#define NBITS(x)   ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, array) (((array)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

#define LIN_HAT_MAX 4

typedef struct
{
    int            fd;
    u64            stable_id;
    char           path[64];
    char           name[128];
    Mel_Array(int) abs_map;
    Mel_Array(int) key_map;
    u32            axis_count;
    u32            button_count;
    u32            hat_count;
    Mel_Array(i16) axes;
    Mel_Array(u8)  buttons;
    Mel_Array(u8)  hats;
    i16            ff_rumble_id;
} Lin_Pad;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Lin_Pad) pads;
} Lin_Backend;

static Lin_Backend g_backend;

static Lin_Pad* pad_for(u64 stable_id)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
        if (g_backend.pads.items[i].stable_id == stable_id && g_backend.pads.items[i].fd >= 0)
            return &g_backend.pads.items[i];
    return NULL;
}

static u64 stable_id_from_path(const char* path)
{
    u64 h = 1469598103934665603ULL;
    for (const char* p = path; *p; p++)
    {
        h ^= (u8)*p;
        h *= 1099511628211ULL;
    }
    return h;
}

static void pad_free(Lin_Pad* pad)
{
    mel_array_free(&pad->abs_map);
    mel_array_free(&pad->key_map);
    mel_array_free(&pad->axes);
    mel_array_free(&pad->buttons);
    mel_array_free(&pad->hats);
}

static void pads_close_all(void)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
    {
        if (g_backend.pads.items[i].fd >= 0)
            close(g_backend.pads.items[i].fd);
        pad_free(&g_backend.pads.items[i]);
    }
    mel_array_clear(&g_backend.pads);
}

static bool open_device(const char* node, Lin_Pad* pad)
{
    int fd = open(node, O_RDWR | O_NONBLOCK);
    if (fd < 0)
        fd = open(node, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;

    unsigned long evbit[NBITS(EV_MAX)] = { 0 };
    unsigned long keybit[NBITS(KEY_MAX)] = { 0 };
    unsigned long absbit[NBITS(ABS_MAX)] = { 0 };
    ioctl(fd, EVIOCGBIT(0, sizeof evbit), evbit);
    if (!TEST_BIT(EV_KEY, evbit) || !TEST_BIT(EV_ABS, evbit))
    {
        close(fd);
        return false;
    }
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybit), keybit);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbit), absbit);

    bool is_joystick = TEST_BIT(BTN_GAMEPAD, keybit) || TEST_BIT(BTN_JOYSTICK, keybit);
    if (!is_joystick)
    {
        close(fd);
        return false;
    }

    *pad = (Lin_Pad){ 0 };
    pad->fd = fd;
    pad->ff_rumble_id = -1;
    mel_array_init(&pad->abs_map, g_backend.alloc);
    mel_array_init(&pad->key_map, g_backend.alloc);
    mel_array_init(&pad->axes, g_backend.alloc);
    mel_array_init(&pad->buttons, g_backend.alloc);
    mel_array_init(&pad->hats, g_backend.alloc);
    strncpy(pad->path, node, sizeof pad->path - 1);
    pad->stable_id = stable_id_from_path(node);
    if (ioctl(fd, EVIOCGNAME(sizeof pad->name), pad->name) < 0)
        pad->name[0] = '\0';

    u32 axis = 0;
    for (int code = 0; code < ABS_CNT; code++)
    {
        int slot = -1;
        if (TEST_BIT(code, absbit))
        {
            bool is_hat = (code == ABS_HAT0X || code == ABS_HAT0Y || code == ABS_HAT1X || code == ABS_HAT1Y || code == ABS_HAT2X || code == ABS_HAT2Y || code == ABS_HAT3X || code == ABS_HAT3Y);
            if (!is_hat)
                slot = (int)axis++;
        }
        mel_array_push(&pad->abs_map, slot);
    }
    pad->axis_count = axis;

    u32 btn = 0;
    for (int code = 0; code < KEY_CNT; code++)
    {
        int slot = -1;
        if (TEST_BIT(code, keybit) && code >= BTN_MISC && code < KEY_MAX)
            slot = (int)btn++;
        mel_array_push(&pad->key_map, slot);
    }
    pad->button_count = btn;

    pad->hat_count = 0;
    for (int h = 0; h < LIN_HAT_MAX; h++)
        if (TEST_BIT(ABS_HAT0X + h * 2, absbit))
            pad->hat_count++;

    for (u32 i = 0; i < pad->axis_count; i++)
        mel_array_push(&pad->axes, (i16)0);
    for (u32 i = 0; i < pad->button_count; i++)
        mel_array_push(&pad->buttons, (u8)0);
    for (u32 i = 0; i < LIN_HAT_MAX; i++)
        mel_array_push(&pad->hats, (u8)MEL_JOYSTICK_HAT_CENTERED);

    return true;
}

static u32 lin_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    pads_close_all();

    DIR* d = opendir("/dev/input");
    if (!d)
        return 0;
    struct dirent* e;
    u32            n = 0;
    while ((e = readdir(d)) != NULL && n < cap)
    {
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        char node[64];
        snprintf(node, sizeof node, "/dev/input/%s", e->d_name);
        Lin_Pad pad;
        if (!open_device(node, &pad))
            continue;

        struct input_id id = { 0 };
        ioctl(pad.fd, EVIOCGID, &id);

        Mel_Joystick_Descriptor desc;
        memset(&desc, 0, sizeof desc);
        desc.name = str8_from_cstr(pad.name);
        desc.vendor_id = id.vendor;
        desc.product_id = id.product;
        desc.version = id.version;
        desc.guid = mel_guid_from_hidapi(id.bustype, id.vendor, id.product, id.version, pad.name, 0, 0);
        desc.axis_count = pad.axis_count;
        desc.button_count = pad.button_count;
        desc.hat_count = pad.hat_count;
        desc.player_index = -1;

        unsigned long ffbit[NBITS(FF_MAX)] = { 0 };
        if (ioctl(pad.fd, EVIOCGBIT(EV_FF, sizeof ffbit), ffbit) >= 0)
            desc.features.dual_motor_rumble = TEST_BIT(FF_RUMBLE, ffbit);

        out[n].stable_id = pad.stable_id;
        out[n].desc = desc;
        mel_array_push(&g_backend.pads, pad);
        n++;
    }
    closedir(d);
    return n;
}

static bool lin_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    (void)user;
    Lin_Pad* pad = pad_for(stable_id);
    if (!pad)
        return false;
    struct input_event ev;
    ssize_t            r;
    while ((r = read(pad->fd, &ev, sizeof ev)) == (ssize_t)sizeof ev)
    {
        if (ev.type == EV_KEY)
        {
            if (ev.code < pad->key_map.count && pad->key_map.items[ev.code] >= 0)
                pad->buttons.items[pad->key_map.items[ev.code]] = ev.value ? 1 : 0;
        }
        else if (ev.type == EV_ABS)
        {
            if (ev.code == ABS_HAT0X || ev.code == ABS_HAT1X || ev.code == ABS_HAT2X || ev.code == ABS_HAT3X)
            {
                int hi = (ev.code - ABS_HAT0X) / 2;
                pad->hats.items[hi] = (u8)((pad->hats.items[hi] & ~(MEL_JOYSTICK_HAT_LEFT | MEL_JOYSTICK_HAT_RIGHT)) | (ev.value < 0 ? MEL_JOYSTICK_HAT_LEFT : ev.value > 0 ? MEL_JOYSTICK_HAT_RIGHT : 0));
            }
            else if (ev.code == ABS_HAT0Y || ev.code == ABS_HAT1Y || ev.code == ABS_HAT2Y || ev.code == ABS_HAT3Y)
            {
                int hi = (ev.code - ABS_HAT0Y) / 2;
                pad->hats.items[hi] = (u8)((pad->hats.items[hi] & ~(MEL_JOYSTICK_HAT_UP | MEL_JOYSTICK_HAT_DOWN)) | (ev.value < 0 ? MEL_JOYSTICK_HAT_UP : ev.value > 0 ? MEL_JOYSTICK_HAT_DOWN : 0));
            }
            else if (ev.code < pad->abs_map.count && pad->abs_map.items[ev.code] >= 0)
            {
                pad->axes.items[pad->abs_map.items[ev.code]] = (i16)ev.value;
            }
        }
    }
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return false;

    memset(out, 0, sizeof *out);
    out->axes = pad->axes.items;
    out->axis_count = pad->axis_count;
    out->buttons = pad->buttons.items;
    out->button_count = pad->button_count;
    out->hats = pad->hats.items;
    out->hat_count = pad->hat_count;
    return true;
}

static Mel_Joystick_Status lin_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    (void)user;
    Lin_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;

    struct ff_effect effect;
    memset(&effect, 0, sizeof effect);
    effect.type = FF_RUMBLE;
    effect.id = pad->ff_rumble_id;
    effect.u.rumble.strong_magnitude = (u16)(rumble.low_frequency * 65535.0f);
    effect.u.rumble.weak_magnitude = (u16)(rumble.high_frequency * 65535.0f);
    effect.replay.length = rumble.duration_s > 0.0f ? (u16)(rumble.duration_s * 1000.0f) : 0xFFFF;
    if (ioctl(pad->fd, EVIOCSFF, &effect) < 0)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
    pad->ff_rumble_id = effect.id;

    struct input_event play;
    memset(&play, 0, sizeof play);
    play.type = EV_FF;
    play.code = (u16)effect.id;
    play.value = 1;
    if (write(pad->fd, &play, sizeof play) != (ssize_t)sizeof play)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    return MEL_JOYSTICK_OK;
}

static void lin_close(void* user, u64 stable_id)
{
    (void)user;
    for (usize i = 0; i < g_backend.pads.count; i++)
    {
        Lin_Pad* pad = &g_backend.pads.items[i];
        if (pad->stable_id != stable_id)
            continue;
        if (pad->fd >= 0)
            close(pad->fd);
        pad_free(pad);
        *pad = (Lin_Pad){ .fd = -1, .ff_rumble_id = -1 };
        return;
    }
}

void mel_joystick__register_host_providers(const Mel_Alloc* alloc)
{
    g_backend.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_backend.pads, g_backend.alloc);
    Mel_Joystick_Provider_Desc desc = {
        .name = "evdev",
        .enumerate = lin_enumerate,
        .poll = lin_poll,
        .rumble = lin_rumble,
        .close = lin_close,
    };
    mel_joystick_provider_register(&desc);
}

i32 mel_joystick_linux_evdev_fd(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return -1;
    Lin_Pad* pad = pad_for(stable_id);
    return pad ? pad->fd : -1;
}

const char* mel_joystick_linux_evdev_path(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return "";
    Lin_Pad* pad = pad_for(stable_id);
    return pad ? pad->path : "";
}
