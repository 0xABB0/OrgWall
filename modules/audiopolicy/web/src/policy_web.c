#include "../../src/audiopolicy_internal.h"

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "policy_web.c is for the emscripten runtime only"
#endif

#include <log/log.h>

#include <emscripten.h>
#include <emscripten/em_js.h>

typedef struct
{
    bool probe_alive;
    bool interrupted;
    bool focus_logged;
} Policy_Web;

static Policy_Web g_web;

EM_JS(int, audiopolicy_web__js_startup, (void), {
    if (globalThis.MelAPol)
        return globalThis.MelAPol.ctx ? (globalThis.MelAPol.ctx.state == 'running' ? 2 : 1) : 0;
    var S = { ctx : null, onstate : null };
    globalThis.MelAPol = S;
    var Ctor = typeof window != 'undefined' ? (window.AudioContext || window.webkitAudioContext) : (typeof AudioContext != 'undefined' ? AudioContext : null);
    if (!Ctor)
        return 0;
    try
    {
        S.ctx = new Ctor();
    }
    catch (e)
    {
        S.ctx = null;
        return 0;
    }
    S.onstate = function() { _mel_audiopolicy_web__on_state(S.ctx.state == 'running' ? 1 : 0); };
    S.ctx.addEventListener('statechange', S.onstate);
    if (S.ctx.resume)
        S.ctx.resume().catch(function(){});
    return S.ctx.state == 'running' ? 2 : 1;
});

EM_JS(void, audiopolicy_web__js_shutdown, (void), {
    var S = globalThis.MelAPol;
    if (!S)
        return;
    if (S.ctx)
    {
        if (S.onstate)
            S.ctx.removeEventListener('statechange', S.onstate);
        if (S.ctx.close)
            S.ctx.close().catch(function(){});
    }
    globalThis.MelAPol = undefined;
});

EMSCRIPTEN_KEEPALIVE void mel_audiopolicy_web__on_state(int running)
{
    if (!g_web.probe_alive)
        return;
    if (running && g_web.interrupted)
    {
        g_web.interrupted = false;
        Mel_AudioPolicy_Event ev = { .interruption_ended = true, .should_resume = true };
        mel_audiopolicy__emit(&ev);
        mel_log_info("audiopolicy", "web: AudioContext resumed; interruption ended, resuming is appropriate");
    }
    else if (!running && !g_web.interrupted)
    {
        g_web.interrupted = true;
        Mel_AudioPolicy_Event ev = { .interruption_began = true };
        mel_audiopolicy__emit(&ev);
        mel_log_info("audiopolicy", "web: AudioContext suspended; interruption began");
    }
}

static void policy_startup(void)
{
    int probe = audiopolicy_web__js_startup();
    if (probe == 0)
    {
        mel_log_warn("audiopolicy", "web: Web Audio unavailable; autoplay-policy interruptions cannot be observed and events never fire");
        return;
    }
    g_web.probe_alive = true;
    mel_log_info("audiopolicy", "web: no OS session/arbitration surface; policy knobs lower with named warnings, autoplay-policy gating surfaces as interruption events");
    if (probe == 1)
    {
        g_web.interrupted = true;
        Mel_AudioPolicy_Event ev = { .interruption_began = true };
        mel_audiopolicy__emit(&ev);
        mel_log_info("audiopolicy", "web: AudioContext suspended at creation (autoplay policy); audio gated until a user gesture");
    }
}

static void policy_shutdown(void)
{
    audiopolicy_web__js_shutdown();
    g_web = (Policy_Web){ 0 };
}

static Mel_AudioPolicy_Status policy_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    u32 bits = 0;
    *in_force = *requested;

    if (requested->mode != &mel_audiopolicy_mode_default)
    {
        bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;
        in_force->mode = &mel_audiopolicy_mode_default;
    }
    if (requested->duck_others)
    {
        bits |= MEL_AUDIOPOLICY_WARN_DUCK_IGNORED;
        in_force->duck_others = false;
    }
    if (requested->allow_bluetooth || requested->allow_bluetooth_a2dp)
        bits |= MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED;
    if (requested->default_to_speaker)
    {
        bits |= MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
        in_force->default_to_speaker = false;
    }

    in_force->mix_with_others = true;
    return bits;
}

static Mel_AudioPolicy_Status policy_focus_request(Mel_AudioPolicy_Focus_Opt opt)
{
    MEL_UNUSED(opt);
    if (!g_web.focus_logged)
    {
        g_web.focus_logged = true;
        mel_log_info("audiopolicy", "web: no focus arbitration model; request granted and loss events never fire");
    }
    return MEL_AUDIOPOLICY_OK;
}

static void policy_focus_abandon(void) {}

static const Mel_AudioPolicy_Backend WEB_BACKEND = {
    .apply = policy_apply,
    .focus_request = policy_focus_request,
    .focus_abandon = policy_focus_abandon,
    .startup = policy_startup,
    .shutdown = policy_shutdown,
};

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &WEB_BACKEND; }
