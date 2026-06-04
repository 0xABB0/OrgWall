#include <vibration/provider.h>

#include "../vibration_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <math.h>
#include <string.h>

typedef struct
{
    u64                  stable_id;
    GUID                 instance;
    LPDIRECTINPUTDEVICE8 device;
    LPDIRECTINPUTEFFECT  effect;
    GUID                 effect_guid;
    bool                 acquired;
    bool                 playing;
    char                 name[128];
} Dinput_Node;

typedef struct
{
    const Mel_Alloc* alloc;
    LPDIRECTINPUT8   di;
    HWND             hwnd;
    Mel_Array(Dinput_Node) nodes;
} Dinput_State;

static Dinput_State ds;

static u64 guid_to_id(const GUID* g)
{
    u64 a = ((u64)g->Data1 << 32) | ((u64)g->Data2 << 16) | (u64)g->Data3;
    u64 b = 0;
    for (int i = 0; i < 8; i++)
        b = (b << 8) | g->Data4[i];
    return a ^ b;
}

static Dinput_Node* node_by_id(u64 stable_id)
{
    for (usize i = 0; i < ds.nodes.count; i++)
        if (ds.nodes.items[i].stable_id == stable_id)
            return &ds.nodes.items[i];
    return NULL;
}

static BOOL CALLBACK enum_cb(const DIDEVICEINSTANCE* inst, void* ctx)
{
    MEL_UNUSED(ctx);
    Dinput_Node node;
    memset(&node, 0, sizeof node);
    node.instance = inst->guidInstance;
    node.stable_id = guid_to_id(&inst->guidInstance);
    node.effect_guid = GUID_ConstantForce;
    WideCharToMultiByte(CP_UTF8, 0, inst->tszProductName, -1, node.name, sizeof node.name - 1, NULL, NULL);

    LPDIRECTINPUTDEVICE8 dev = NULL;
    if (FAILED(IDirectInput8_CreateDevice(ds.di, &inst->guidInstance, &dev, NULL)))
        return DIENUM_CONTINUE;
    IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick);
    if (ds.hwnd)
        IDirectInputDevice8_SetCooperativeLevel(dev, ds.hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    node.device = dev;
    mel_array_push(&ds.nodes, node);
    return DIENUM_CONTINUE;
}

static u32 dinput_enumerate(void* user, Mel_Vib_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (!ds.di)
        return 0;

    for (usize i = 0; i < ds.nodes.count; i++)
    {
        if (ds.nodes.items[i].effect)
            IDirectInputEffect_Release(ds.nodes.items[i].effect);
        if (ds.nodes.items[i].device)
            IDirectInputDevice8_Release(ds.nodes.items[i].device);
    }
    ds.nodes.count = 0;

    IDirectInput8_EnumDevices(ds.di, DI8DEVCLASS_GAMECTRL, enum_cb, NULL, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);

    for (usize i = 0; i < ds.nodes.count && i < cap; i++)
    {
        out[i].stable_id = ds.nodes.items[i].stable_id;
        out[i].name = str8_from_cstr(ds.nodes.items[i].name[0] ? ds.nodes.items[i].name : "DirectInput force-feedback");
        memset(&out[i].caps, 0, sizeof out[i].caps);
        out[i].caps.present = true;
        out[i].caps.amplitude = true;
        out[i].caps.continuous = true;
        out[i].caps.actuator_count = 1;
    }
    return (u32)ds.nodes.count;
}

static bool dinput_open(void* user, u64 stable_id, Mel_Vib_Descriptor* out)
{
    MEL_UNUSED(user);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->device)
        return false;
    if (!n->acquired)
    {
        IDirectInputDevice8_Acquire(n->device);
        n->acquired = true;
    }
    memset(out, 0, sizeof *out);
    out->caps.present = true;
    out->caps.amplitude = true;
    out->caps.continuous = true;
    out->caps.actuator_count = 1;
    return true;
}

static void dinput_close(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n)
        return;
    if (n->effect)
    {
        IDirectInputEffect_Stop(n->effect);
        IDirectInputEffect_Release(n->effect);
        n->effect = NULL;
    }
    if (n->device && n->acquired)
    {
        IDirectInputDevice8_Unacquire(n->device);
        n->acquired = false;
    }
}

static bool dinput_ff_query(void* user, u64 stable_id, Mel_Vib_FF_Caps* out)
{
    MEL_UNUSED(user);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->device)
        return false;
    memset(out, 0, sizeof *out);
    out->present = true;
    out->effects = MEL_VIB_FF_EFFECT_CONSTANT | MEL_VIB_FF_EFFECT_RAMP | MEL_VIB_FF_EFFECT_PERIODIC | MEL_VIB_FF_EFFECT_CONDITION;
    out->waveforms = MEL_VIB_FF_WAVE_SINE | MEL_VIB_FF_WAVE_SQUARE | MEL_VIB_FF_WAVE_TRIANGLE | MEL_VIB_FF_WAVE_SAWTOOTH_UP | MEL_VIB_FF_WAVE_SAWTOOTH_DOWN;
    out->conditions = MEL_VIB_FF_COND_SPRING | MEL_VIB_FF_COND_DAMPER | MEL_VIB_FF_COND_INERTIA | MEL_VIB_FF_COND_FRICTION;
    out->direction_axes = 2;
    out->gain = true;
    out->autocenter = true;
    out->autocenter_continuous = false;
    out->envelope = true;
    out->max_effects = 1;
    out->min_frequency_hz = 0.0f;
    out->max_frequency_hz = 0.0f;
    return true;
}

static const GUID* effect_guid(u32 effect, const Mel_Vib_FF_Effect* fx)
{
    if (effect == MEL_VIB_FF_EFFECT_CONSTANT || effect == MEL_VIB_FF_EFFECT_RUMBLE)
        return &GUID_ConstantForce;
    if (effect == MEL_VIB_FF_EFFECT_RAMP)
        return &GUID_RampForce;
    if (effect == MEL_VIB_FF_EFFECT_PERIODIC)
    {
        switch (fx->periodic.waveform)
        {
        case MEL_VIB_FF_WAVE_SQUARE:
            return &GUID_Square;
        case MEL_VIB_FF_WAVE_TRIANGLE:
            return &GUID_Triangle;
        case MEL_VIB_FF_WAVE_SAWTOOTH_UP:
            return &GUID_SawtoothUp;
        case MEL_VIB_FF_WAVE_SAWTOOTH_DOWN:
            return &GUID_SawtoothDown;
        default:
            return &GUID_Sine;
        }
    }
    if (effect == MEL_VIB_FF_EFFECT_CONDITION && fx->condition_count > 0 && fx->conditions)
    {
        switch (fx->conditions[0].kind)
        {
        case MEL_VIB_FF_COND_DAMPER:
            return &GUID_Damper;
        case MEL_VIB_FF_COND_INERTIA:
            return &GUID_Inertia;
        case MEL_VIB_FF_COND_FRICTION:
            return &GUID_Friction;
        default:
            return &GUID_Spring;
        }
    }
    return &GUID_ConstantForce;
}

static LONG mag10k(f32 v)
{
    if (v < -1.0f)
        v = -1.0f;
    if (v > 1.0f)
        v = 1.0f;
    return (LONG)(v * 10000.0f);
}

static DWORD umag10k(f32 v)
{
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    return (DWORD)(v * 10000.0f);
}

static DWORD dir_angle_hundredths(const Mel_Vib_FF_Direction* dir)
{
    f32 rad = 0.0f;
    if (dir->encoding == MEL_VIB_FF_DIR_POLAR)
        rad = dir->a;
    else if (dir->encoding == MEL_VIB_FF_DIR_CARTESIAN)
        rad = atan2f(dir->b, dir->a);
    else if (dir->encoding == MEL_VIB_FF_DIR_SPHERICAL)
        rad = dir->a;
    else
        rad = (dir->a >= 0.0f) ? 1.5707963f : 4.7123890f;
    f32 deg = rad * (180.0f / 3.14159265f);
    while (deg < 0.0f)
        deg += 360.0f;
    return (DWORD)(deg * 100.0f);
}

static Mel_Vib_Status dinput_build(Dinput_Node* n, const Mel_Vib_FF_Lowered* low)
{
    const Mel_Vib_FF_Effect* fx = &low->effect;

    DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG  dir[2] = { 0, 0 };
    dir[0] = (LONG)dir_angle_hundredths(&fx->direction);

    DICONSTANTFORCE cf;
    DIRAMPFORCE     rf;
    DIPERIODIC      pf;
    DICONDITION     cond[2];
    DIENVELOPE      env;
    memset(&env, 0, sizeof env);
    env.dwSize = sizeof env;
    env.dwAttackTime = (DWORD)(fx->envelope.attack_s * 1000000.0f);
    env.dwAttackLevel = umag10k(fx->envelope.attack_level);
    env.dwFadeTime = (DWORD)(fx->envelope.fade_s * 1000000.0f);
    env.dwFadeLevel = umag10k(fx->envelope.fade_level);
    bool has_env = (fx->envelope.attack_s > 0.0f || fx->envelope.fade_s > 0.0f);

    DIEFFECT eff;
    memset(&eff, 0, sizeof eff);
    eff.dwSize = sizeof eff;
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    if (fx->direction.encoding == MEL_VIB_FF_DIR_POLAR || fx->direction.encoding == MEL_VIB_FF_DIR_STEERING_AXIS)
        eff.dwFlags = DIEFF_POLAR | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = (fx->duration_s <= 0.0f) ? INFINITE : (DWORD)(fx->duration_s * 1000000.0f);
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwStartDelay = (DWORD)(fx->start_delay_s * 1000000.0f);
    eff.cAxes = 2;
    eff.rgdwAxes = axes;
    eff.rglDirection = dir;
    eff.lpEnvelope = has_env ? &env : NULL;

    if (fx->effect == MEL_VIB_FF_EFFECT_CONSTANT || fx->effect == MEL_VIB_FF_EFFECT_RUMBLE)
    {
        cf.lMagnitude = mag10k(fx->constant.magnitude);
        eff.cbTypeSpecificParams = sizeof cf;
        eff.lpvTypeSpecificParams = &cf;
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_RAMP)
    {
        rf.lStart = mag10k(fx->ramp.start);
        rf.lEnd = mag10k(fx->ramp.end);
        eff.cbTypeSpecificParams = sizeof rf;
        eff.lpvTypeSpecificParams = &rf;
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_PERIODIC)
    {
        pf.dwMagnitude = umag10k(fx->periodic.magnitude);
        pf.lOffset = mag10k(fx->periodic.offset);
        pf.dwPhase = (DWORD)(fx->periodic.phase * 36000.0f);
        pf.dwPeriod = (fx->periodic.frequency_hz > 0.0f) ? (DWORD)(1000000.0f / fx->periodic.frequency_hz) : 0;
        eff.cbTypeSpecificParams = sizeof pf;
        eff.lpvTypeSpecificParams = &pf;
    }
    else if (fx->effect == MEL_VIB_FF_EFFECT_CONDITION && fx->condition_count > 0 && fx->conditions)
    {
        const Mel_Vib_FF_Condition* c = &fx->conditions[0];
        for (int i = 0; i < 2; i++)
        {
            memset(&cond[i], 0, sizeof cond[i]);
            cond[i].lOffset = mag10k(c->center);
            cond[i].lPositiveCoefficient = mag10k(c->right_coeff);
            cond[i].lNegativeCoefficient = mag10k(c->left_coeff);
            cond[i].dwPositiveSaturation = umag10k(c->right_saturation);
            cond[i].dwNegativeSaturation = umag10k(c->left_saturation);
            cond[i].lDeadBand = (LONG)umag10k(c->deadband);
        }
        eff.cbTypeSpecificParams = sizeof(DICONDITION) * 2;
        eff.lpvTypeSpecificParams = cond;
        eff.lpEnvelope = NULL;
    }
    else
    {
        return MEL_VIB_ERROR;
    }

    const GUID* g = effect_guid(fx->effect, fx);
    if (n->effect && IsEqualGUID(g, &n->effect_guid))
    {
        if (FAILED(IDirectInputEffect_SetParameters(n->effect, &eff, DIEP_ALLPARAMS | DIEP_NORESTART)))
            return MEL_VIB_ERROR;
        return MEL_VIB_OK;
    }

    if (n->effect)
    {
        IDirectInputEffect_Stop(n->effect);
        IDirectInputEffect_Release(n->effect);
        n->effect = NULL;
    }
    n->effect_guid = *g;
    if (FAILED(IDirectInputDevice8_CreateEffect(n->device, g, &eff, &n->effect, NULL)))
    {
        mel_log_error("vibration", "DirectInput CreateEffect failed for %s", n->name);
        return MEL_VIB_ERROR;
    }
    return MEL_VIB_OK;
}

static Mel_Vib_Status dinput_ff_upload(void* user, u64 stable_id, u64 effect_token, const Mel_Vib_FF_Lowered* low)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->device)
        return MEL_VIB_ERROR;
    return dinput_build(n, low);
}

static Mel_Vib_Status dinput_ff_update(void* user, u64 stable_id, u64 effect_token, const Mel_Vib_FF_Lowered* low)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->device)
        return MEL_VIB_ERROR;
    return dinput_build(n, low);
}

static Mel_Vib_Status dinput_ff_start(void* user, u64 stable_id, u64 effect_token, u32 loop)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->effect)
        return MEL_VIB_ERROR;
    DWORD iters = (loop == 0u) ? 1 : (loop == 0xFFFFFFFFu ? INFINITE : loop);
    if (FAILED(IDirectInputEffect_Start(n->effect, iters, 0)))
        return MEL_VIB_ERROR;
    n->playing = true;
    return MEL_VIB_OK;
}

static Mel_Vib_Status dinput_ff_stop(void* user, u64 stable_id, u64 effect_token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->effect)
        return MEL_VIB_ERROR;
    IDirectInputEffect_Stop(n->effect);
    n->playing = false;
    return MEL_VIB_OK;
}

static void dinput_ff_release(void* user, u64 stable_id, u64 effect_token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(effect_token);
    Dinput_Node* n = node_by_id(stable_id);
    if (n && n->effect)
    {
        IDirectInputEffect_Stop(n->effect);
        IDirectInputEffect_Release(n->effect);
        n->effect = NULL;
        n->playing = false;
    }
}

static Mel_Vib_Status dinput_ff_set_gain(void* user, u64 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->device)
        return MEL_VIB_ERROR;
    DIPROPDWORD prop;
    memset(&prop, 0, sizeof prop);
    prop.diph.dwSize = sizeof prop;
    prop.diph.dwHeaderSize = sizeof prop.diph;
    prop.diph.dwObj = 0;
    prop.diph.dwHow = DIPH_DEVICE;
    prop.dwData = (DWORD)(gain * 10000.0f);
    if (FAILED(IDirectInputDevice8_SetProperty(n->device, DIPROP_FFGAIN, &prop.diph)))
        return MEL_VIB_ERROR;
    return MEL_VIB_OK;
}

static Mel_Vib_Status dinput_ff_set_autocenter(void* user, u64 stable_id, bool enabled, f32 strength)
{
    MEL_UNUSED(user);
    MEL_UNUSED(strength);
    Dinput_Node* n = node_by_id(stable_id);
    if (!n || !n->device)
        return MEL_VIB_ERROR;
    DIPROPDWORD prop;
    memset(&prop, 0, sizeof prop);
    prop.diph.dwSize = sizeof prop;
    prop.diph.dwHeaderSize = sizeof prop.diph;
    prop.diph.dwObj = 0;
    prop.diph.dwHow = DIPH_DEVICE;
    prop.dwData = enabled ? DIPROPAUTOCENTER_ON : DIPROPAUTOCENTER_OFF;
    if (FAILED(IDirectInputDevice8_SetProperty(n->device, DIPROP_AUTOCENTER, &prop.diph)))
        return MEL_VIB_ERROR;
    return MEL_VIB_OK;
}

void mel_vib__register_host_providers(void)
{
    const Mel_Alloc* alloc = mel_vib__alloc();
    ds.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&ds.nodes, ds.alloc);
    ds.hwnd = GetActiveWindow();

    if (FAILED(DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8, (void**)&ds.di, NULL)))
    {
        mel_log_warn("vibration", "DirectInput8Create failed; no win32 force-feedback");
        return;
    }

    Mel_Vib_Provider_Desc desc;
    memset(&desc, 0, sizeof desc);
    desc.name = "dinput-ff";
    desc.enumerate = dinput_enumerate;
    desc.open = dinput_open;
    desc.close = dinput_close;
    desc.ff_query = dinput_ff_query;
    desc.ff_upload = dinput_ff_upload;
    desc.ff_update = dinput_ff_update;
    desc.ff_start = dinput_ff_start;
    desc.ff_stop = dinput_ff_stop;
    desc.ff_release = dinput_ff_release;
    desc.ff_set_gain = dinput_ff_set_gain;
    desc.ff_set_autocenter = dinput_ff_set_autocenter;
    mel_vib_provider_register(&desc);
}
