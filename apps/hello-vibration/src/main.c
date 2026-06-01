#include <stdio.h>

#include <app/app.h>
#include <gui/gui.h>
#include <reactor/reactor.h>
#include <vibration/vibration.h>
#include <log/log.h>

static Mel_Reactor*     g_reactor;
static Mel_Vib_Device   g_dev = MEL_VIB_DEVICE_NULL;
static Mel_Vib_Playback g_pb = MEL_VIB_PLAYBACK_NULL;
static Mel_Gui_Handle   g_status;

static const Mel_Vib_Event g_events[] = {
    { .at = 0.0f, .duration = 0.0f, .intensity = 1.0f, .sharpness = 0.9f },
    { .at = 0.25f, .duration = 1.25f, .intensity = 0.5f, .sharpness = 0.4f },
    { .at = 1.75f, .duration = 1.5f, .intensity = 0.9f, .sharpness = 0.6f },
};

static const Mel_Vib_Pattern g_pattern = { .events = g_events, .count = 3, .loop = 0 };

static void set_status(const char* fmt, ...)
{
    char    buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    mel_log_info("vib-demo", "%s", buf);
    if (!mel_gui_handle_is_none(g_status))
        mel_gui_set_text(g_status, str8_from_cstr(buf));
}

static void on_done(Mel_Vib_Playback pb, Mel_Vib_Status status, void* user)
{
    (void)pb;
    (void)user;
    g_pb = MEL_VIB_PLAYBACK_NULL;
    set_status("resolved status=0x%x", status);
}

static void play_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (!mel_vib_alive(g_dev))
    {
        set_status("no vibration device present");
        return;
    }
    Mel_Vib_Play_Result r = mel_vib_play(g_dev, &g_pattern, .reactor = g_reactor, .on_complete = on_done);
    g_pb = r.value;
    set_status("PLAY status=0x%x playing=%d", r.status, (int)mel_vib_playing(g_pb));
}

static void pause_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    Mel_Vib_Status s = mel_vib_pause(g_pb);
    set_status("PAUSE status=0x%x paused=%d", s, (int)mel_vib_paused(g_pb));
}

static void resume_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    Mel_Vib_Status s = mel_vib_resume(g_pb);
    set_status("RESUME status=0x%x playing=%d", s, (int)mel_vib_playing(g_pb));
}

static void abort_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_vib_abort(g_pb);
    g_pb = MEL_VIB_PLAYBACK_NULL;
    set_status("ABORT");
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Vibration"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    char info[128];
    if (mel_vib_alive(g_dev))
    {
        Mel_Vib_Describe_Result d = mel_vib_describe(g_dev);
        snprintf(info, sizeof info, "device: %.*s  amp=%d sharp=%d pause=%d",
                 (int)d.value.name.len, (const char*)d.value.name.data,
                 (int)d.value.caps.amplitude, (int)d.value.caps.sharpness, (int)d.value.caps.can_pause);
    }
    else
        snprintf(info, sizeof info, "no vibration device (count=%u)", mel_vib_count());
    mel_label_create(frame, .text = str8_from_cstr(info), .layoutable = { .preferred_h = 28 });

    mel_button_create(frame, .text = S8("Play"), .pointer.on_click = play_clicked, .layoutable = { .preferred_h = 48 });
    mel_button_create(frame, .text = S8("Pause"), .pointer.on_click = pause_clicked, .layoutable = { .preferred_h = 48 });
    mel_button_create(frame, .text = S8("Resume"), .pointer.on_click = resume_clicked, .layoutable = { .preferred_h = 48 });
    mel_button_create(frame, .text = S8("Abort"), .pointer.on_click = abort_clicked, .layoutable = { .preferred_h = 48 });

    g_status = mel_label_create(frame, .text = S8("tap Play"), .layoutable = { .preferred_h = 48 });
}

void mel_app_setup(Mel_Reactor* reactor)
{
    g_reactor = reactor;
    mel_gui_init(reactor);
    mel_vib_init(NULL, reactor);

    u32 n = mel_vib_count();
    mel_log_info("vib-demo", "vibration devices: %u", n);
    if (n > 0)
        mel_vib_list(&g_dev, 1);

    mel_app_register_screen(S8("main"), .build = build_main);
    mel_app_present(S8("main"), NULL);
}
