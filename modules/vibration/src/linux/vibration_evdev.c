#include <vibration/provider.h>

#include "../vibration_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MEL_EVDEV_BITS(n)    (((n) + 7) / 8)
#define MEL_EVDEV_TEST(a, b) ((a)[(b) / 8] & (1u << ((b) % 8)))

typedef struct
{
    u64  stable_id;
    int  fd;
    char path[32];
    i16  effect_id;
    bool uploaded;
    bool playing;
} Evdev_Node;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Evdev_Node) nodes;
} Evdev_State;

static Evdev_State es;

static Evdev_Node* node_by_id(u64 stable_id)
{
    for (usize i = 0; i < es.nodes.count; i++)
        if (es.nodes.items[i].stable_id == stable_id)
            return &es.nodes.items[i];
    return NULL;
}

static bool node_supports_ff(int fd, u64* out_caps_effects, u64* out_caps_waves, u64* out_caps_conds)
{
    unsigned long ff_bits[MEL_EVDEV_BITS(FF_MAX) / sizeof(unsigned long) + 1];
    memset(ff_bits, 0, sizeof ff_bits);
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff_bits), ff_bits) < 0)
        return false;

    u64                  effects = 0, waves = 0, conds = 0;
    const unsigned char* b = (const unsigned char*)ff_bits;
    if (MEL_EVDEV_TEST(b, FF_RUMBLE))
        effects |= MEL_VIB_FF_EFFECT_RUMBLE;
    if (MEL_EVDEV_TEST(b, FF_CONSTANT))
        effects |= MEL_VIB_FF_EFFECT_CONSTANT;
    if (MEL_EVDEV_TEST(b, FF_RAMP))
        effects |= MEL_VIB_FF_EFFECT_RAMP;
    if (MEL_EVDEV_TEST(b, FF_PERIODIC))
        effects |= MEL_VIB_FF_EFFECT_PERIODIC;
    if (MEL_EVDEV_TEST(b, FF_SPRING))
    {
        effects |= MEL_VIB_FF_EFFECT_CONDITION;
        conds |= MEL_VIB_FF_COND_SPRING;
    }
    if (MEL_EVDEV_TEST(b, FF_DAMPER))
    {
        effects |= MEL_VIB_FF_EFFECT_CONDITION;
        conds |= MEL_VIB_FF_COND_DAMPER;
    }
    if (MEL_EVDEV_TEST(b, FF_INERTIA))
    {
        effects |= MEL_VIB_FF_EFFECT_CONDITION;
        conds |= MEL_VIB_FF_COND_INERTIA;
    }
    if (MEL_EVDEV_TEST(b, FF_FRICTION))
    {
        effects |= MEL_VIB_FF_EFFECT_CONDITION;
        conds |= MEL_VIB_FF_COND_FRICTION;
    }
    if (MEL_EVDEV_TEST(b, FF_SINE))
        waves |= MEL_VIB_FF_WAVE_SINE;
    if (MEL_EVDEV_TEST(b, FF_SQUARE))
        waves |= MEL_VIB_FF_WAVE_SQUARE;
    if (MEL_EVDEV_TEST(b, FF_TRIANGLE))
        waves |= MEL_VIB_FF_WAVE_TRIANGLE;
    if (MEL_EVDEV_TEST(b, FF_SAW_UP))
        waves |= MEL_VIB_FF_WAVE_SAWTOOTH_UP;
    if (MEL_EVDEV_TEST(b, FF_SAW_DOWN))
        waves |= MEL_VIB_FF_WAVE_SAWTOOTH_DOWN;

    if (effects == 0)
        return false;
    *out_caps_effects = effects;
    *out_caps_waves = waves;
    *out_caps_conds = conds;
    return true;
}

static u32 evdev_enumerate(void* user, Mel_Vib_Raw* out, u32 cap)
{
    MEL_UNUSED(user);

    for (usize i = 0; i < es.nodes.count; i++)
    {
        if (es.nodes.items[i].fd >= 0)
            close(es.nodes.items[i].fd);
    }
    es.nodes.count = 0;

    DIR* d = opendir("/dev/input");
    if (!d)
        return 0;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;
        char path[32];
        snprintf(path, sizeof path, "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;

        u64 effects = 0, waves = 0, conds = 0;
        if (!node_supports_ff(fd, &effects, &waves, &conds))
        {
            close(fd);
            continue;
        }

        struct input_id id;
        memset(&id, 0, sizeof id);
        ioctl(fd, EVIOCGID, &id);
        u64 sid = ((u64)id.vendor << 48) | ((u64)id.product << 32) | (u64)strtoul(ent->d_name + 5, NULL, 10);

        int n_effects = 0;
        ioctl(fd, EVIOCGEFFECTS, &n_effects);

        Evdev_Node node;
        memset(&node, 0, sizeof node);
        node.stable_id = sid;
        node.fd = fd;
        node.effect_id = -1;
        snprintf(node.path, sizeof node.path, "%s", path);
        mel_array_push(&es.nodes, node);

        if (es.nodes.count - 1 < cap)
        {
            char namebuf[128];
            memset(namebuf, 0, sizeof namebuf);
            ioctl(fd, EVIOCGNAME(sizeof namebuf - 1), namebuf);
            out[es.nodes.count - 1].stable_id = sid;
            out[es.nodes.count - 1].name = str8_from_cstr(namebuf[0] ? namebuf : "evdev force-feedback");
            memset(&out[es.nodes.count - 1].caps, 0, sizeof out[es.nodes.count - 1].caps);
            out[es.nodes.count - 1].caps.present = true;
            out[es.nodes.count - 1].caps.amplitude = true;
            out[es.nodes.count - 1].caps.continuous = true;
            out[es.nodes.count - 1].caps.actuator_count = 1;
            out[es.nodes.count - 1].caps.max_events = (u32)(n_effects > 0 ? n_effects : 1);
        }
    }
    closedir(d);
    return (u32)es.nodes.count;
}

static bool evdev_open(void* user, u64 stable_id, Mel_Vib_Descriptor* out)
{
    MEL_UNUSED(user);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n)
        return false;
    memset(out, 0, sizeof *out);
    out->caps.present = true;
    out->caps.amplitude = true;
    out->caps.continuous = true;
    out->caps.actuator_count = 1;
    return true;
}

static void evdev_close(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    Evdev_Node* n = node_by_id(stable_id);
    if (n && n->fd >= 0)
    {
        if (n->effect_id >= 0)
            ioctl(n->fd, EVIOCRMFF, n->effect_id);
        close(n->fd);
        n->fd = -1;
        n->effect_id = -1;
    }
}

static bool evdev_ff_query(void* user, u64 stable_id, Mel_Vib_FF_Caps* out)
{
    MEL_UNUSED(user);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0)
        return false;
    u64 effects = 0, waves = 0, conds = 0;
    if (!node_supports_ff(n->fd, &effects, &waves, &conds))
        return false;
    int n_effects = 0;
    ioctl(n->fd, EVIOCGEFFECTS, &n_effects);
    memset(out, 0, sizeof *out);
    out->present = true;
    out->effects = effects;
    out->waveforms = waves;
    out->conditions = conds;
    out->direction_axes = 2;
    out->gain = true;
    out->autocenter = true;
    out->autocenter_continuous = true;
    out->envelope = true;
    out->max_effects = (u32)(n_effects > 0 ? n_effects : 1);
    out->min_frequency_hz = 0.0f;
    out->max_frequency_hz = 0.0f;
    return true;
}

static u16 clamp_dir_polar(const Mel_Vib_FF_Direction* dir)
{
    f32 rad = 0.0f;
    if (dir->encoding == MEL_VIB_FF_DIR_POLAR)
        rad = dir->a;
    else if (dir->encoding == MEL_VIB_FF_DIR_CARTESIAN)
        rad = atan2f(dir->b, dir->a);
    else if (dir->encoding == MEL_VIB_FF_DIR_SPHERICAL)
        rad = dir->a;
    else if (dir->encoding == MEL_VIB_FF_DIR_STEERING_AXIS)
        rad = (dir->a >= 0.0f) ? 1.5707963f : 4.7123890f;
    f32 norm = rad / 6.2831853f;
    norm -= floorf(norm);
    return (u16)(norm * 65535.0f);
}

static i16 mag16(f32 v)
{
    if (v < -1.0f)
        v = -1.0f;
    if (v > 1.0f)
        v = 1.0f;
    return (i16)(v * 32767.0f);
}

static u16 umag16(f32 v)
{
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    return (u16)(v * 65535.0f);
}

static u16 wave_to_evdev(u32 w)
{
    switch (w)
    {
    case MEL_VIB_FF_WAVE_SINE:
        return FF_SINE;
    case MEL_VIB_FF_WAVE_SQUARE:
        return FF_SQUARE;
    case MEL_VIB_FF_WAVE_TRIANGLE:
        return FF_TRIANGLE;
    case MEL_VIB_FF_WAVE_SAWTOOTH_UP:
        return FF_SAW_UP;
    case MEL_VIB_FF_WAVE_SAWTOOTH_DOWN:
        return FF_SAW_DOWN;
    default:
        return FF_SINE;
    }
}

static u16 cond_to_evdev(u32 c)
{
    switch (c)
    {
    case MEL_VIB_FF_COND_SPRING:
        return FF_SPRING;
    case MEL_VIB_FF_COND_DAMPER:
        return FF_DAMPER;
    case MEL_VIB_FF_COND_INERTIA:
        return FF_INERTIA;
    case MEL_VIB_FF_COND_FRICTION:
        return FF_FRICTION;
    default:
        return FF_SPRING;
    }
}

static Mel_Vib_Status evdev_build(Evdev_Node* n, const Mel_Vib_FF_Lowered* low)
{
    const Mel_Vib_FF_Effect* fx = &low->effect;
    struct ff_effect         e;
    memset(&e, 0, sizeof e);
    e.id = n->effect_id;
    e.direction = clamp_dir_polar(&fx->direction);
    e.replay.length = (fx->duration_s <= 0.0f) ? 0 : (u16)(fx->duration_s * 1000.0f);
    e.replay.delay = (u16)(fx->start_delay_s * 1000.0f);

    if (fx->effect == MEL_VIB_FF_EFFECT_RUMBLE)
    {
        e.type = FF_RUMBLE;
        e.u.rumble.strong_magnitude = umag16(fx->constant.magnitude);
        e.u.rumble.weak_magnitude = umag16(fx->constant.magnitude * 0.5f);
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_CONSTANT)
    {
        e.type = FF_CONSTANT;
        e.u.constant.level = mag16(fx->constant.magnitude);
        e.u.constant.envelope.attack_length = (u16)(fx->envelope.attack_s * 1000.0f);
        e.u.constant.envelope.attack_level = umag16(fx->envelope.attack_level);
        e.u.constant.envelope.fade_length = (u16)(fx->envelope.fade_s * 1000.0f);
        e.u.constant.envelope.fade_level = umag16(fx->envelope.fade_level);
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_RAMP)
    {
        e.type = FF_RAMP;
        e.u.ramp.start_level = mag16(fx->ramp.start);
        e.u.ramp.end_level = mag16(fx->ramp.end);
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_PERIODIC)
    {
        e.type = FF_PERIODIC;
        e.u.periodic.waveform = wave_to_evdev(fx->periodic.waveform);
        e.u.periodic.magnitude = mag16(fx->periodic.magnitude);
        e.u.periodic.offset = mag16(fx->periodic.offset);
        e.u.periodic.phase = (u16)(fx->periodic.phase * 65535.0f);
        e.u.periodic.period = (fx->periodic.frequency_hz > 0.0f) ? (u16)(1000.0f / fx->periodic.frequency_hz) : 0;
        e.u.periodic.envelope.attack_length = (u16)(fx->envelope.attack_s * 1000.0f);
        e.u.periodic.envelope.attack_level = umag16(fx->envelope.attack_level);
        e.u.periodic.envelope.fade_length = (u16)(fx->envelope.fade_s * 1000.0f);
        e.u.periodic.envelope.fade_level = umag16(fx->envelope.fade_level);
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_CONDITION && fx->condition_count > 0 && fx->conditions)
    {
        const Mel_Vib_FF_Condition* c = &fx->conditions[0];
        e.type = cond_to_evdev(c->kind);
        for (int axis = 0; axis < 2; axis++)
        {
            e.u.condition[axis].right_saturation = umag16(c->right_saturation);
            e.u.condition[axis].left_saturation = umag16(c->left_saturation);
            e.u.condition[axis].right_coeff = mag16(c->right_coeff);
            e.u.condition[axis].left_coeff = mag16(c->left_coeff);
            e.u.condition[axis].deadband = umag16(c->deadband);
            e.u.condition[axis].center = mag16(c->center);
        }
    }
    else
    {
        return MEL_VIB_ERROR;
    }

    if (ioctl(n->fd, EVIOCSFF, &e) < 0)
    {
        mel_log_error("vibration", "evdev EVIOCSFF failed on %s", n->path);
        return MEL_VIB_ERROR;
    }
    n->effect_id = e.id;
    n->uploaded = true;
    return MEL_VIB_OK;
}

static Mel_Vib_Status evdev_ff_upload(void* user, u64 stable_id, u64 effect_token, const Mel_Vib_FF_Lowered* low)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0)
        return MEL_VIB_ERROR;
    n->effect_id = -1;
    return evdev_build(n, low);
}

static Mel_Vib_Status evdev_ff_update(void* user, u64 stable_id, u64 effect_token, const Mel_Vib_FF_Lowered* low)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0 || !n->uploaded)
        return MEL_VIB_ERROR;
    return evdev_build(n, low);
}

static Mel_Vib_Status evdev_ff_start(void* user, u64 stable_id, u64 effect_token, u32 loop)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0 || !n->uploaded)
        return MEL_VIB_ERROR;
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = (u16)n->effect_id;
    ev.value = (loop == 0u) ? 1 : (int)loop;
    if (write(n->fd, &ev, sizeof ev) != (ssize_t)sizeof ev)
        return MEL_VIB_ERROR;
    n->playing = true;
    return MEL_VIB_OK;
}

static Mel_Vib_Status evdev_ff_stop(void* user, u64 stable_id, u64 effect_token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0 || n->effect_id < 0)
        return MEL_VIB_ERROR;
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = (u16)n->effect_id;
    ev.value = 0;
    if (write(n->fd, &ev, sizeof ev) != (ssize_t)sizeof ev)
        return MEL_VIB_ERROR;
    n->playing = false;
    return MEL_VIB_OK;
}

static void evdev_ff_release(void* user, u64 stable_id, u64 effect_token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Evdev_Node* n = node_by_id(stable_id);
    if (n && n->fd >= 0 && n->effect_id >= 0)
    {
        ioctl(n->fd, EVIOCRMFF, n->effect_id);
        n->effect_id = -1;
        n->uploaded = false;
        n->playing = false;
    }
}

static Mel_Vib_Status evdev_ff_set_gain(void* user, u64 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0)
        return MEL_VIB_ERROR;
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = FF_GAIN;
    ev.value = (int)(gain * 65535.0f);
    if (write(n->fd, &ev, sizeof ev) != (ssize_t)sizeof ev)
        return MEL_VIB_ERROR;
    return MEL_VIB_OK;
}

static Mel_Vib_Status evdev_ff_set_autocenter(void* user, u64 stable_id, bool enabled, f32 strength)
{
    MEL_UNUSED(user);
    Evdev_Node* n = node_by_id(stable_id);
    if (!n || n->fd < 0)
        return MEL_VIB_ERROR;
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = FF_AUTOCENTER;
    ev.value = enabled ? (int)(strength * 65535.0f) : 0;
    if (write(n->fd, &ev, sizeof ev) != (ssize_t)sizeof ev)
        return MEL_VIB_ERROR;
    return MEL_VIB_OK;
}

void mel_vib__register_host_providers(void)
{
    const Mel_Alloc* alloc = mel_vib__alloc();
    es.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&es.nodes, es.alloc);

    Mel_Vib_Provider_Desc desc;
    memset(&desc, 0, sizeof desc);
    desc.name = "evdev-ff";
    desc.enumerate = evdev_enumerate;
    desc.open = evdev_open;
    desc.close = evdev_close;
    desc.ff_query = evdev_ff_query;
    desc.ff_upload = evdev_ff_upload;
    desc.ff_update = evdev_ff_update;
    desc.ff_start = evdev_ff_start;
    desc.ff_stop = evdev_ff_stop;
    desc.ff_release = evdev_ff_release;
    desc.ff_set_gain = evdev_ff_set_gain;
    desc.ff_set_autocenter = evdev_ff_set_autocenter;
    mel_vib_provider_register(&desc);
}
