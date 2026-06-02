#include <stdio.h>

#include <string/str8.h>

#include "hud.h"
#include "gpu_host.h"

static const char* bindless_tier_name(Mel_Gpu_Bindless_Tier t)
{
    switch (t)
    {
    case MEL_GPU_TIER_FULL:
        return "full";
    case MEL_GPU_TIER_CAPPED:
        return "capped";
    case MEL_GPU_TIER_NONE:
    default:
        return "none";
    }
}

void hud_init(Hud* hud, Mel_Gpu_Device* dev)
{
    *hud = (Hud){ 0 };
    if (!dev)
    {
        snprintf(hud->prefix, sizeof hud->prefix, "no device");
        return;
    }
    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    snprintf(hud->prefix, sizeof hud->prefix, "%s · bindless %s", caps->adapter.name, bindless_tier_name(caps->memory.bindless.tier));
}

void hud_frame(Hud* hud, f64 dt, const char* suffix)
{
    // Seed on the first non-zero dt, then exponentially smooth so the readout is
    // legible instead of jittering frame to frame.
    if (dt > 0.0)
        hud->ewma_dt = (hud->ewma_dt == 0.0) ? dt : hud->ewma_dt * 0.9 + dt * 0.1;

    hud->accum += dt;
    hud->frames++;
    if (hud->accum < 0.2) // refresh the label ~5×/second, not every frame
        return;
    hud->accum = 0.0;
    hud->frames = 0;

    f64  fps = (hud->ewma_dt > 0.0) ? 1.0 / hud->ewma_dt : 0.0;
    char buf[256];
    if (suffix && *suffix)
        snprintf(buf, sizeof buf, "%s · %.0f fps · %s", hud->prefix, fps, suffix);
    else
        snprintf(buf, sizeof buf, "%s · %.0f fps", hud->prefix, fps);
    gpu_host_set_status(str8_from_cstr(buf));
}
