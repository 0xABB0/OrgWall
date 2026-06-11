#include "clip_linux.h"

#include <log/log.h>

#include <stdlib.h>

typedef struct
{
    bool tried;
    bool x11;
    bool wl;
} Linux_Clip;

static Linux_Clip g_lin;

static void linux_clip_ensure(void)
{
    if (g_lin.tried)
        return;
    g_lin.tried = true;

    const char* force = getenv("MEL_CLIP_BACKEND");
    bool        want_wl = force && (force[0] == 'w' || force[0] == 'W');
    bool        want_x11 = force && (force[0] == 'x' || force[0] == 'X');

    if (!want_wl && mel_clip__x11_init())
    {
        g_lin.x11 = true;
        mel_log_info("clipboard", "linux backend: X11 selections (CLIPBOARD + PRIMARY)");
        return;
    }
    if (!want_x11 && mel_clip__wl_init())
    {
        g_lin.wl = true;
        mel_log_warn("clipboard", "linux backend: Wayland connected, same-process selection cache only (no cross-client wl_data_device serving)");
        return;
    }
    mel_log_error("clipboard", "linux backend: neither X11 nor Wayland available (no DISPLAY/WAYLAND_DISPLAY)");
}

bool mel_clip__plat_available(void)
{
    linux_clip_ensure();
    return g_lin.x11 || g_lin.wl;
}

void mel_clip__plat_shutdown(void)
{
    if (g_lin.x11)
        mel_clip__x11_shutdown();
    if (g_lin.wl)
        mel_clip__wl_shutdown();
    g_lin = (Linux_Clip){ 0 };
}

bool mel_clip__plat_channel_supported(Mel_Clip_Channel ch)
{
    Mel_Clip_Channel c = mel_clip_channel_resolve(ch);
    linux_clip_ensure();
    if (g_lin.x11 || g_lin.wl)
        return c == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_CLIPBOARD || c == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_PRIMARY;
    return false;
}

u64 mel_clip__plat_sequence(Mel_Clip_Channel ch)
{
    linux_clip_ensure();
    if (g_lin.x11)
        return mel_clip__x11_sequence(mel_clip_channel_resolve(ch));
    if (g_lin.wl)
        return mel_clip__wl_sequence(mel_clip_channel_resolve(ch));
    return 0;
}

void mel_clip__plat_read(Mel_Clip_Job* job)
{
    if (g_lin.x11)
        mel_clip__x11_read(job);
    else if (g_lin.wl)
        mel_clip__wl_read(job);
    else
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
}

void mel_clip__plat_write(Mel_Clip_Job* job)
{
    if (g_lin.x11)
        mel_clip__x11_write(job);
    else if (g_lin.wl)
        mel_clip__wl_write(job);
    else
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
}

void mel_clip__plat_clear(Mel_Clip_Job* job)
{
    if (g_lin.x11)
        mel_clip__x11_clear(job);
    else if (g_lin.wl)
        mel_clip__wl_clear(job);
    else
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
}

void mel_clip__plat_query(Mel_Clip_Job* job)
{
    if (g_lin.x11)
        mel_clip__x11_query(job);
    else if (g_lin.wl)
        mel_clip__wl_query(job);
    else
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
}

void mel_clip__plat_has(Mel_Clip_Job* job)
{
    if (g_lin.x11)
        mel_clip__x11_has(job);
    else if (g_lin.wl)
        mel_clip__wl_has(job);
    else
    {
        mel_clip_job_set_present(job, false);
        mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD);
    }
}

void* mel_clip__plat_native(void)
{
    if (g_lin.x11)
        return mel_clip__x11_native();
    if (g_lin.wl)
        return mel_clip__wl_native();
    return NULL;
}
