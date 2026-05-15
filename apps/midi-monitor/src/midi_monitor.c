#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <gui.control/button.h>
#include <gui.control/label.h>
#include <gui.control/window.h>
#include <gui.platform/gui.platform.h>

#include <music.midi/midi.h>
#include <music.midi/midi_port.h>

#define DEVICE_ROW_COUNT 6
#define MSG_MIDI_CHUNK   (MEL_GUI_MSG_USER + 1)

typedef struct App_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    Mel_Gui_Handle last_event;
    Mel_Gui_Handle count_label;
    Mel_Gui_Handle device_rows[DEVICE_ROW_COUNT];
    Mel_Midi_Port* port;
    pthread_t      poll_thread;
    volatile bool  poll_running;
    bool           poll_started;
    int32_t        event_count;
    int32_t        device_count;
} App_State;

static App_State g_app;

static void set_status(const char* text)
{
    mel_gui_set_text(g_app.status, str8_from_cstr(text));
}

static void refresh_devices(void)
{
    Mel_Midi_Port_Info infos[DEVICE_ROW_COUNT];
    int32_t n = mel_midi_port_enumerate_inputs(infos, DEVICE_ROW_COUNT);
    g_app.device_count = n;

    for (int32_t i = 0; i < DEVICE_ROW_COUNT; i++) {
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

static void* poll_thread_fn(void* arg)
{
    Mel_Gui_Handle window = *(Mel_Gui_Handle*)arg;
    while (g_app.poll_running) {
        Mel_Midi_Chunk chunk;
        if (g_app.port != NULL && mel_midi_port_poll(g_app.port, &chunk)) {
            uint64_t packed = (uint64_t)chunk.data[0]
                            | ((uint64_t)chunk.data[1] << 8)
                            | ((uint64_t)chunk.data[2] << 16)
                            | ((uint64_t)(uint8_t)chunk.length << 24);
            mel_gui_post_message(window, MSG_MIDI_CHUNK, (Mel_Gui_WParam)packed, 0);
        } else {
            usleep(2000);
        }
    }
    return NULL;
}

static void start_polling(void)
{
    if (g_app.poll_started) return;
    g_app.poll_running = true;
    static Mel_Gui_Handle window_copy;
    window_copy = g_app.window;
    if (pthread_create(&g_app.poll_thread, NULL, poll_thread_fn, &window_copy) == 0) {
        g_app.poll_started = true;
    } else {
        g_app.poll_running = false;
    }
}

static void stop_polling(void)
{
    if (!g_app.poll_started) return;
    g_app.poll_running = false;
    pthread_join(g_app.poll_thread, NULL);
    g_app.poll_started = false;
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

static Mel_Gui_Result refresh_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_CLICK) {
        refresh_devices();
        char text[64];
        snprintf(text, sizeof(text), "Status: found %d device(s)", (int)g_app.device_count);
        set_status(text);
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result connect_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_CLICK) {
        do_connect();
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result disconnect_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_CLICK) {
        do_disconnect();
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static void format_event(uint64_t packed, char* out, size_t n)
{
    Mel_Midi_Chunk chunk = {0};
    chunk.data[0] = (uint8_t)(packed & 0xFF);
    chunk.data[1] = (uint8_t)((packed >> 8) & 0xFF);
    chunk.data[2] = (uint8_t)((packed >> 16) & 0xFF);
    chunk.length  = (int32_t)((packed >> 24) & 0xFF);

    Mel_Midi_Msg msg = {0};
    if (!mel_midi_parse(&chunk, &msg)) {
        snprintf(out, n, "Last event: raw %02X %02X %02X (len=%d)",
            chunk.data[0], chunk.data[1], chunk.data[2], chunk.length);
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

static Mel_Gui_Result window_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MSG_MIDI_CHUNK) {
        g_app.event_count += 1;
        char count_text[48];
        snprintf(count_text, sizeof(count_text), "Events: %d", (int)g_app.event_count);
        mel_gui_set_text(g_app.count_label, str8_from_cstr(count_text));

        char text[160];
        format_event((uint64_t)w, text, sizeof(text));
        mel_gui_set_text(g_app.last_event, str8_from_cstr(text));
        return MEL_GUI_OK;
    }
    if (msg == MEL_GUI_MSG_APP_DESTROY) {
        do_disconnect();
    }
    return mel_gui_call_super(h, msg, w, l);
}

void mel_gui_app_build_activity(str8 activity_name)
{
    (void)activity_name;

    memset(&g_app, 0, sizeof(g_app));

    g_app.window = mel_gui_create_window(S8("MIDI Monitor"), 0, 0, window_proc, &g_app);

    mel_gui_create_label(g_app.window, S8("MIDI Monitor"), 1, 24, 20, 360, 44);
    mel_gui_create_label(g_app.window, S8("Tap Refresh, then Connect to read from the first listed device."),
        2, 24, 70, 380, 44);

    mel_gui_create_button(g_app.window, S8("Refresh"),    10, 24,  126, 110, 52, refresh_button_proc,    NULL);
    mel_gui_create_button(g_app.window, S8("Connect"),    11, 144, 126, 110, 52, connect_button_proc,    NULL);
    mel_gui_create_button(g_app.window, S8("Disconnect"), 12, 264, 126, 130, 52, disconnect_button_proc, NULL);

    mel_gui_create_label(g_app.window, S8("Devices:"), 20, 24, 198, 360, 32);
    for (int i = 0; i < DEVICE_ROW_COUNT; i++) {
        g_app.device_rows[i] = mel_gui_create_label(g_app.window, S8(""), 30 + (u32)i, 24, 234 + i * 32, 380, 30);
    }

    g_app.status      = mel_gui_create_label(g_app.window, S8("Status: idle"),      40, 24, 444, 380, 32);
    g_app.last_event  = mel_gui_create_label(g_app.window, S8("Last event: -"),     41, 24, 480, 380, 32);
    g_app.count_label = mel_gui_create_label(g_app.window, S8("Events: 0"),         42, 24, 516, 380, 32);

    refresh_devices();
}
