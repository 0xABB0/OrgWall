#include <stdio.h>
#include <string.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>
#include <reactor/reactor.h>

#include <midi/midi.h>
#include <midi/midi_port.h>

#include "monitor.h"

#define DEVICE_ROW_COUNT 6
#define POLL_INTERVAL_NS ((i64)1000000)

typedef struct {
    Mel_Reactor*        reactor;
    Mel_Reactor_Source* poll_timer;
    Mel_Midi_Port*      port;
    Mel_Gui_Handle      status;
    Mel_Gui_Handle      last_event;
    Mel_Gui_Handle      count_label;
    Mel_Gui_Handle      device_rows[DEVICE_ROW_COUNT];
    i32                 event_count;
    i32                 device_count;
} Monitor_State;

static Monitor_State g_app;

static void set_status(const char* text)
{
    mel_gui_set_text(g_app.status, str8_from_cstr(text));
}

static void refresh_devices(void)
{
    Mel_Midi_Port_Info infos[DEVICE_ROW_COUNT];
    i32 n = mel_midi_port_enumerate_inputs(infos, DEVICE_ROW_COUNT);
    g_app.device_count = n;

    for (i32 i = 0; i < DEVICE_ROW_COUNT; i++) {
        char row[160];
        if (i < n) {
            snprintf(row, sizeof(row), "[%d] %s", (int)infos[i].id, infos[i].name ? infos[i].name : "(unnamed)");
        } else if (i == 0 && n == 0) {
            snprintf(row, sizeof(row), "(no MIDI input devices found)");
        } else {
            row[0] = 0;
        }
        mel_gui_set_text(g_app.device_rows[i], str8_from_cstr(row));
    }
}

static void format_event(const Mel_Midi_Chunk* chunk, char* out, size_t n)
{
    Mel_Midi_Msg msg = {0};
    if (!mel_midi_parse(chunk, &msg)) {
        snprintf(out, n, "Last event: raw %02X %02X %02X (len=%d)",
            chunk->data[0], chunk->data[1], chunk->data[2], chunk->length);
        return;
    }

    const char* kind = mel_midi_msg_kind_name(msg.kind);
    switch (msg.kind) {
        case MEL_MIDI_MSG_NOTE_ON:
            snprintf(out, n, "Last event: note_on  ch=%u note=%u vel=%u",
                msg.channel, msg.note_on.note, msg.note_on.velocity);
            break;
        case MEL_MIDI_MSG_NOTE_OFF:
            snprintf(out, n, "Last event: note_off ch=%u note=%u vel=%u",
                msg.channel, msg.note_off.note, msg.note_off.velocity);
            break;
        case MEL_MIDI_MSG_CONTROL_CHANGE:
            snprintf(out, n, "Last event: cc       ch=%u ctrl=%u val=%u",
                msg.channel, msg.control_change.controller, msg.control_change.value);
            break;
        case MEL_MIDI_MSG_PITCH_BEND:
            snprintf(out, n, "Last event: pitch    ch=%u bend=%d",
                msg.channel, (int)msg.pitch_bend.bend);
            break;
        case MEL_MIDI_MSG_PROGRAM_CHANGE:
            snprintf(out, n, "Last event: program  ch=%u prog=%u",
                msg.channel, msg.program_change.program);
            break;
        case MEL_MIDI_MSG_CHANNEL_PRESSURE:
            snprintf(out, n, "Last event: chpress  ch=%u val=%u",
                msg.channel, msg.channel_pressure.pressure);
            break;
        case MEL_MIDI_MSG_KEY_PRESSURE:
            snprintf(out, n, "Last event: keypress ch=%u note=%u val=%u",
                msg.channel, msg.key_pressure.note, msg.key_pressure.pressure);
            break;
        default:
            snprintf(out, n, "Last event: %s", kind);
            break;
    }
}

static bool poll_tick(void* user)
{
    (void)user;
    if (g_app.port == NULL) return true;

    Mel_Midi_Chunk chunk;
    bool any = false;
    char text[160];
    while (mel_midi_port_poll(g_app.port, &chunk)) {
        g_app.event_count += 1;
        format_event(&chunk, text, sizeof(text));
        any = true;
    }

    if (any) {
        char count_text[48];
        snprintf(count_text, sizeof(count_text), "Events: %d", (int)g_app.event_count);
        mel_gui_set_text(g_app.count_label, str8_from_cstr(count_text));
        mel_gui_set_text(g_app.last_event, str8_from_cstr(text));
    }
    return true;
}

static void start_polling(void)
{
    if (g_app.poll_timer != NULL) return;
    g_app.poll_timer = mel_reactor_timer_new(POLL_INTERVAL_NS, poll_tick, NULL);
    if (g_app.poll_timer != NULL) {
        mel_reactor_source_attach(g_app.reactor, g_app.poll_timer);
    }
}

static void stop_polling(void)
{
    if (g_app.poll_timer == NULL) return;
    mel_reactor_source_destroy(g_app.poll_timer);
    g_app.poll_timer = NULL;
}

static void do_connect(void)
{
    if (g_app.port != NULL) {
        set_status("Status: already connected");
        return;
    }
    if (g_app.device_count <= 0) {
        set_status("Status: no devices to open (tap Refresh)");
        return;
    }
    g_app.port = mel_midi_port_open_input(0);
    if (g_app.port == NULL) {
        set_status("Status: open failed");
        return;
    }
    const char* name = mel_midi_port_name(g_app.port);
    char text[224];
    snprintf(text, sizeof(text), "Status: connected to \"%s\"", name ? name : "?");
    set_status(text);
    g_app.event_count = 0;
    mel_gui_set_text(g_app.count_label, S8("Events: 0"));
    mel_gui_set_text(g_app.last_event, S8("Last event: -"));
    start_polling();
}

static void do_disconnect(void)
{
    if (g_app.port == NULL) {
        set_status("Status: not connected");
        return;
    }
    stop_polling();
    mel_midi_port_close(g_app.port);
    g_app.port = NULL;
    set_status("Status: disconnected");
}

static void refresh_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    refresh_devices();
    char text[64];
    snprintf(text, sizeof(text), "Status: found %d device(s)", (int)g_app.device_count);
    set_status(text);
}

static void connect_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    do_connect();
}

static void disconnect_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    do_disconnect();
}

void build_monitor(Mel_Gui_Handle frame, void* user)
{
    Mel_Reactor* reactor = (Mel_Reactor*)user;
    memset(&g_app, 0, sizeof(g_app));
    g_app.reactor = reactor;

    mel_gui_set_text(frame, S8("MIDI Monitor"));

    mel_label_create(frame, .text = S8("MIDI Monitor"),
        .x = 24, .y = 20, .w = 360, .h = 44);
    mel_label_create(frame, .text = S8("Tap Refresh, then Connect to read from the first listed device."),
        .x = 24, .y = 70, .w = 380, .h = 44);

    mel_button_create(frame, .text = S8("Refresh"),
        .x = 24, .y = 126, .w = 110, .h = 52,
        .pointer.on_click = refresh_clicked);
    mel_button_create(frame, .text = S8("Connect"),
        .x = 144, .y = 126, .w = 110, .h = 52,
        .pointer.on_click = connect_clicked);
    mel_button_create(frame, .text = S8("Disconnect"),
        .x = 264, .y = 126, .w = 130, .h = 52,
        .pointer.on_click = disconnect_clicked);

    mel_label_create(frame, .text = S8("Devices:"),
        .x = 24, .y = 198, .w = 360, .h = 32);
    for (i32 i = 0; i < DEVICE_ROW_COUNT; i++) {
        g_app.device_rows[i] = mel_label_create(frame, .text = S8(""),
            .x = 24, .y = 234 + i * 32, .w = 380, .h = 30);
    }

    g_app.status      = mel_label_create(frame, .text = S8("Status: idle"),  .x = 24, .y = 444, .w = 380, .h = 32);
    g_app.last_event  = mel_label_create(frame, .text = S8("Last event: -"), .x = 24, .y = 480, .w = 380, .h = 32);
    g_app.count_label = mel_label_create(frame, .text = S8("Events: 0"),     .x = 24, .y = 516, .w = 380, .h = 32);

    refresh_devices();
}
