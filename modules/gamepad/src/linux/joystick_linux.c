#include <gamepad/provider.h>
#include <gamepad/linux/linux.h>

#include "../joystick_backend.h"

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

typedef struct
{
    int   fd;
    u64   stable_id;
    char  path[64];
    char  name[128];
    int   abs_map[ABS_CNT];
    int   key_map[KEY_CNT];
    u32   axis_count;
    u32   button_count;
    u32   hat_count;
    i16   axes[ABS_CNT];
    u8    buttons[KEY_CNT];
    u8    hats[4];
    i16   ff_rumble_id;
} Lin_Pad;

static Lin_Pad g_pads[32];
static u32     g_pad_count;

static Lin_Pad* pad_for(u64 stable_id)
{
    for (u32 i = 0; i < g_pad_count; i++)
        if (g_pads[i].stable_id == stable_id && g_pads[i].fd >= 0)
            return &g_pads[i];
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

    memset(pad, 0, sizeof *pad);
    pad->fd = fd;
    pad->ff_rumble_id = -1;
    strncpy(pad->path, node, sizeof pad->path - 1);
    pad->stable_id = stable_id_from_path(node);
    if (ioctl(fd, EVIOCGNAME(sizeof pad->name), pad->name) < 0)
        pad->name[0] = '\0';

    u32 axis = 0;
    for (int code = 0; code < ABS_CNT; code++)
    {
        pad->abs_map[code] = -1;
        if (TEST_BIT(code, absbit))
        {
            if (code == ABS_HAT0X || code == ABS_HAT0Y || code == ABS_HAT1X || code == ABS_HAT1Y || code == ABS_HAT2X || code == ABS_HAT2Y || code == ABS_HAT3X || code == ABS_HAT3Y)
                continue;
            pad->abs_map[code] = (int)axis++;
        }
    }
    pad->axis_count = axis;

    u32 btn = 0;
    for (int code = 0; code < KEY_CNT; code++)
    {
        pad->key_map[code] = -1;
        if (TEST_BIT(code, keybit) && code >= BTN_MISC && code < KEY_MAX)
            pad->key_map[code] = (int)btn++;
    }
    pad->button_count = btn;

    pad->hat_count = 0;
    for (int h = 0; h < 4; h++)
        if (TEST_BIT(ABS_HAT0X + h * 2, absbit))
            pad->hat_count++;

    return true;
}

static u32 lin_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    for (u32 i = 0; i < g_pad_count; i++)
        if (g_pads[i].fd >= 0)
            close(g_pads[i].fd);
    g_pad_count = 0;

    DIR* d = opendir("/dev/input");
    if (!d)
        return 0;
    struct dirent* e;
    u32            n = 0;
    while ((e = readdir(d)) != NULL && g_pad_count < 32 && n < cap)
    {
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        char node[64];
        snprintf(node, sizeof node, "/dev/input/%s", e->d_name);
        Lin_Pad* pad = &g_pads[g_pad_count];
        if (!open_device(node, pad))
            continue;

        struct input_id id = { 0 };
        ioctl(pad->fd, EVIOCGID, &id);

        Mel_Joystick_Descriptor desc;
        memset(&desc, 0, sizeof desc);
        desc.name = str8_from_cstr(pad->name);
        desc.vendor_id = id.vendor;
        desc.product_id = id.product;
        desc.version = id.version;
        desc.guid = mel_guid_from_hidapi(id.bustype, id.vendor, id.product, id.version, pad->name, 0, 0);
        desc.axis_count = pad->axis_count;
        desc.button_count = pad->button_count;
        desc.hat_count = pad->hat_count;
        desc.player_index = -1;

        unsigned long ffbit[NBITS(FF_MAX)] = { 0 };
        if (ioctl(pad->fd, EVIOCGBIT(EV_FF, sizeof ffbit), ffbit) >= 0)
            desc.features.dual_motor_rumble = TEST_BIT(FF_RUMBLE, ffbit);

        out[n].stable_id = pad->stable_id;
        out[n].desc = desc;
        g_pad_count++;
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
            if (ev.code < KEY_CNT && pad->key_map[ev.code] >= 0)
                pad->buttons[pad->key_map[ev.code]] = ev.value ? 1 : 0;
        }
        else if (ev.type == EV_ABS)
        {
            if (ev.code == ABS_HAT0X || ev.code == ABS_HAT1X || ev.code == ABS_HAT2X || ev.code == ABS_HAT3X)
            {
                int hi = (ev.code - ABS_HAT0X) / 2;
                pad->hats[hi] = (u8)((pad->hats[hi] & ~(MEL_JOYSTICK_HAT_LEFT | MEL_JOYSTICK_HAT_RIGHT)) | (ev.value < 0 ? MEL_JOYSTICK_HAT_LEFT : ev.value > 0 ? MEL_JOYSTICK_HAT_RIGHT : 0));
            }
            else if (ev.code == ABS_HAT0Y || ev.code == ABS_HAT1Y || ev.code == ABS_HAT2Y || ev.code == ABS_HAT3Y)
            {
                int hi = (ev.code - ABS_HAT0Y) / 2;
                pad->hats[hi] = (u8)((pad->hats[hi] & ~(MEL_JOYSTICK_HAT_UP | MEL_JOYSTICK_HAT_DOWN)) | (ev.value < 0 ? MEL_JOYSTICK_HAT_UP : ev.value > 0 ? MEL_JOYSTICK_HAT_DOWN : 0));
            }
            else if (ev.code < ABS_CNT && pad->abs_map[ev.code] >= 0)
            {
                pad->axes[pad->abs_map[ev.code]] = (i16)ev.value;
            }
        }
    }
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return false;

    memset(out, 0, sizeof *out);
    out->axes = pad->axes;
    out->axis_count = pad->axis_count;
    out->buttons = pad->buttons;
    out->button_count = pad->button_count;
    out->hats = pad->hats;
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
    Lin_Pad* pad = pad_for(stable_id);
    if (pad && pad->fd >= 0)
    {
        close(pad->fd);
        pad->fd = -1;
    }
}

void mel_joystick__register_host_providers(void)
{
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
