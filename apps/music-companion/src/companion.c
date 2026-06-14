#include <stdio.h>
#include <string.h>

#include <core/platform.h>
#include <gui/gui.h>
#include <vat/tick.h>
#include <vat/vat.h>
#include <allocator/heap.h>

#include <midi/midi.h>
#include <midi/midi_port.h>
#include <musictuning/tuning.h>
#include <musictheory/pitch.h>
#include <musictheory/scale.h>
#include <musicnotation/western.h>
#include <frequency/cent.h>
#include <audiocapture/audiocapture.h>
#include <audioin/audioin.h>
#include <audioin/permission.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>
#include <pitchdetect/pitchdetect.h>

#include "companion.h"

#define POLL_INTERVAL_NS  ((i64)1000000)
#define HELD_DISPLAY_MAX  12
#define TUNER_SAMPLE_RATE 48000u
#define TUNER_WINDOW      2048u
#define TUNER_HOP         1024u
#define TUNER_HOLD_TICKS  24

typedef struct
{
    Mel_Vat*       vat;
    Mel_Vat_Tick*  poll_timer;
    Mel_Midi_Port* port;

    Mel_Tuning         tuning;
    Mel_NatAccNotation western;
    Mel_Chord_Catalog  catalog;

    bool held[128];

    Mel_Gui_Handle status;
    Mel_Gui_Handle held_label;
    Mel_Gui_Handle chord_label;
    Mel_Gui_Handle detail_label;

    Mel_AudioCapture* cap;
    Mel_PitchDetector detector;
    Mel_Vat_Tick*     tuner_timer;
    f32               window[TUNER_WINDOW];
    u32               window_fill;
    i32               unvoiced_ticks;

    Mel_Gui_Handle tuner_status;
    Mel_Gui_Handle tuner_note;
    Mel_Gui_Handle tuner_cents;
} Companion_State;

static Companion_State g_app;

static void set_status(const char* text) { mel_gui_set_text(g_app.status, str8_from_cstr(text)); }

static usize append_note_name(char* out, usize cap, usize at, i32 midi_note)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Pitch pitch = mel_pitch_make(&g_app.tuning, mel_western_midi_to_index(midi_note));
    Mel_Note  note = mel_notation_guess_note(&g_app.western.base, pitch);
    str8      symbol = mel_nat_acc_note_symbol(&g_app.western, note, alloc);

    int written = snprintf(out + at, cap - at, "%.*s%d ", (int)symbol.len, (const char*)symbol.data, (int)(note.nat_bi_index + 4));
    if (symbol.data)
        mel_dealloc(alloc, symbol.data);
    return written > 0 ? at + (usize)written : at;
}

static void describe_dyad(char* out, usize cap, i32 lo, i32 hi)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Note na = mel_notation_guess_note(&g_app.western.base, mel_pitch_make(&g_app.tuning, mel_western_midi_to_index(lo)));
    Mel_Note nb = mel_notation_guess_note(&g_app.western.base, mel_pitch_make(&g_app.tuning, mel_western_midi_to_index(hi)));

    Mel_NoteInterval ni = mel_nat_acc_interval(&g_app.western, na, nb);
    str8             symbol = mel_nat_acc_interval_symbol(&g_app.western, ni, alloc);

    snprintf(out, cap, "Interval: %.*s", (int)symbol.len, (const char*)symbol.data);
    if (symbol.data)
        mel_dealloc(alloc, symbol.data);
}

static void describe_chord(char* out, usize cap, char* detail, usize detail_cap, const i32* notes, i32 count)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Scale pcs = mel_scale_make(alloc, &g_app.tuning);
    for (i32 i = 0; i < count; i++)
        mel_scale_add_index(&pcs, mel_pitch_pc_index(mel_pitch_make(&g_app.tuning, mel_western_midi_to_index(notes[i]))));

    i64 bass_pc = mel_pitch_pc_index(mel_pitch_make(&g_app.tuning, mel_western_midi_to_index(notes[0])));

    Mel_Chord_Match_Array matches = mel_chord_identify(alloc, &g_app.catalog, &pcs, bass_pc);

    if (matches.count == 0)
    {
        snprintf(out, cap, "Chord: ?");
        snprintf(detail, detail_cap, "(no catalog match for %d held notes)", (int)count);
    }
    else
    {
        Mel_Chord_Match m = matches.items[0];

        Mel_Note root = mel_notation_guess_note(&g_app.western.base, mel_pitch_make(&g_app.tuning, m.root_pc));
        str8     root_str = mel_nat_acc_note_symbol(&g_app.western, root, alloc);
        str8     quality = g_app.catalog.entries.items[m.quality].name;

        if (m.bass_member != 0)
        {
            Mel_Note bass = mel_notation_guess_note(&g_app.western.base, mel_pitch_make(&g_app.tuning, bass_pc));
            str8     bass_str = mel_nat_acc_note_symbol(&g_app.western, bass, alloc);
            snprintf(out, cap, "Chord: %.*s%.*s/%.*s", (int)root_str.len, (const char*)root_str.data, (int)quality.len, (const char*)quality.data, (int)bass_str.len, (const char*)bass_str.data);
            if (bass_str.data)
                mel_dealloc(alloc, bass_str.data);
        }
        else
        {
            snprintf(out, cap, "Chord: %.*s%.*s", (int)root_str.len, (const char*)root_str.data, (int)quality.len, (const char*)quality.data);
        }

        if (matches.count > 1)
            snprintf(detail, detail_cap, "(%d alternative reading(s))", (int)(matches.count - 1));
        else
            detail[0] = 0;

        if (root_str.data)
            mel_dealloc(alloc, root_str.data);
    }

    mel_array_free(&matches);
    mel_scale_free(&pcs);
}

static void update_display(void)
{
    i32 notes[128];
    i32 count = 0;
    for (i32 n = 0; n < 128; n++)
        if (g_app.held[n])
            notes[count++] = n;

    char held_text[256];
    char chord_text[128];
    char detail_text[128];
    detail_text[0] = 0;

    if (count == 0)
    {
        snprintf(held_text, sizeof(held_text), "Held: -");
        snprintf(chord_text, sizeof(chord_text), "Chord: -");
    }
    else
    {
        usize at = (usize)snprintf(held_text, sizeof(held_text), "Held: ");
        for (i32 i = 0; i < count && i < HELD_DISPLAY_MAX; i++)
            at = append_note_name(held_text, sizeof(held_text), at, notes[i]);
        if (count > HELD_DISPLAY_MAX)
            snprintf(held_text + at, sizeof(held_text) - at, "(+%d)", (int)(count - HELD_DISPLAY_MAX));

        if (count == 1)
            snprintf(chord_text, sizeof(chord_text), "Chord: (single note)");
        else if (count == 2)
            describe_dyad(chord_text, sizeof(chord_text), notes[0], notes[1]);
        else
            describe_chord(chord_text, sizeof(chord_text), detail_text, sizeof(detail_text), notes, count);
    }

    mel_gui_set_text(g_app.held_label, str8_from_cstr(held_text));
    mel_gui_set_text(g_app.chord_label, str8_from_cstr(chord_text));
    mel_gui_set_text(g_app.detail_label, str8_from_cstr(detail_text));
}

static bool poll_tick(void* user)
{
    MEL_UNUSED(user);
    if (g_app.port == NULL)
        return true;

    Mel_Midi_Chunk chunk;
    bool           changed = false;
    while (mel_midi_port_poll(g_app.port, &chunk))
    {
        Mel_Midi_Msg msg = { 0 };
        if (!mel_midi_parse(&chunk, &msg))
            continue;

        if (msg.kind == MEL_MIDI_MSG_NOTE_ON && msg.note_on.velocity > 0)
        {
            g_app.held[msg.note_on.note] = true;
            changed = true;
        }
        else if (msg.kind == MEL_MIDI_MSG_NOTE_OFF || (msg.kind == MEL_MIDI_MSG_NOTE_ON && msg.note_on.velocity == 0))
        {
            u8 note = msg.kind == MEL_MIDI_MSG_NOTE_OFF ? msg.note_off.note : msg.note_on.note;
            g_app.held[note] = false;
            changed = true;
        }
    }

    if (changed)
        update_display();
    return true;
}

static void do_connect(void)
{
    if (g_app.port != NULL)
    {
        set_status("Status: already connected");
        return;
    }

    Mel_Midi_Port_Info infos[8];
    i32                n = mel_midi_port_enumerate_inputs(infos, 8);
    if (n <= 0)
    {
        set_status("Status: no MIDI input devices found");
        return;
    }

    g_app.port = mel_midi_port_open_input(infos[0].id);
    if (g_app.port == NULL)
    {
        set_status("Status: open failed");
        return;
    }

    const char* name = mel_midi_port_name(g_app.port);
    char        text[224];
    snprintf(text, sizeof(text), "Status: listening on \"%s\"", name ? name : "?");
    set_status(text);

    memset(g_app.held, 0, sizeof(g_app.held));
    update_display();

    if (g_app.poll_timer == NULL)
        g_app.poll_timer = mel_vat_tick_open(g_app.vat, mel_alloc_heap(), POLL_INTERVAL_NS, poll_tick, NULL);
}

static void do_disconnect(void)
{
    if (g_app.port == NULL)
    {
        set_status("Status: not connected");
        return;
    }
    if (g_app.poll_timer != NULL)
    {
        mel_vat_tick_close(g_app.poll_timer);
        g_app.poll_timer = NULL;
    }
    mel_midi_port_close(g_app.port);
    g_app.port = NULL;
    set_status("Status: disconnected");
}

static void connect_clicked(Mel_Gui_Handle h, void* user)
{
    MEL_UNUSED(h);
    MEL_UNUSED(user);
    do_connect();
}

static void disconnect_clicked(Mel_Gui_Handle h, void* user)
{
    MEL_UNUSED(h);
    MEL_UNUSED(user);
    do_disconnect();
}

static void build_chords_tab(Mel_Gui_Handle tab)
{
    mel_label_create(tab, .text = S8("Attach a MIDI device, play notes, read the chord."), .x = 24, .y = 16, .w = 480, .h = 32);

    mel_button_create(tab, .text = S8("Connect"), .x = 24, .y = 60, .w = 120, .h = 48, .pointer.on_click = connect_clicked);
    mel_button_create(tab, .text = S8("Disconnect"), .x = 156, .y = 60, .w = 130, .h = 48, .pointer.on_click = disconnect_clicked);

    g_app.status = mel_label_create(tab, .text = S8("Status: idle"), .x = 24, .y = 124, .w = 480, .h = 30);
    g_app.held_label = mel_label_create(tab, .text = S8("Held: -"), .x = 24, .y = 162, .w = 480, .h = 30);
    g_app.chord_label = mel_label_create(tab, .text = S8("Chord: -"), .x = 24, .y = 200, .w = 480, .h = 40);
    g_app.detail_label = mel_label_create(tab, .text = S8(""), .x = 24, .y = 244, .w = 480, .h = 30);
}

static void tuner_show_estimate(Mel_Pitch_Estimate est)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Hz   detected = mel_freq(est.frequency_hz);
    i64      idx = mel_tuning_find_index(&g_app.tuning, detected);
    Mel_Hz   target = mel_tuning_frequency_for_index(&g_app.tuning, idx);
    f64      cents = mel_cent_to_double(mel_cent_from_freqs_c(&detected, &target));
    Mel_Note note = mel_notation_guess_note(&g_app.western.base, mel_pitch_make(&g_app.tuning, idx));
    str8     symbol = mel_nat_acc_note_symbol(&g_app.western, note, alloc);

    char text[96];
    snprintf(text, sizeof(text), "%.*s%d", (int)symbol.len, (const char*)symbol.data, (int)(note.nat_bi_index + 4));
    mel_gui_set_text(g_app.tuner_note, str8_from_cstr(text));
    if (symbol.data)
        mel_dealloc(alloc, symbol.data);

    snprintf(text, sizeof(text), "%+.1f cents  (%.2f Hz, clarity %.0f%%)", cents, est.frequency_hz, (f64)est.clarity * 100.0);
    mel_gui_set_text(g_app.tuner_cents, str8_from_cstr(text));
}

static bool tuner_tick(void* user)
{
    MEL_UNUSED(user);
    if (g_app.cap == NULL)
        return true;

    bool detected_any = false;
    for (;;)
    {
        u32 got = mel_audiocapture_read(g_app.cap, g_app.window + g_app.window_fill, TUNER_WINDOW - g_app.window_fill);
        g_app.window_fill += got;
        if (g_app.window_fill < TUNER_WINDOW)
            break;

        Mel_Pitch_Estimate est = mel_pitch_detect(&g_app.detector, g_app.window, TUNER_WINDOW);
        if (est.voiced)
        {
            tuner_show_estimate(est);
            g_app.unvoiced_ticks = 0;
            detected_any = true;
        }

        memmove(g_app.window, g_app.window + TUNER_HOP, (TUNER_WINDOW - TUNER_HOP) * sizeof(f32));
        g_app.window_fill = TUNER_WINDOW - TUNER_HOP;
    }

    if (!detected_any && g_app.unvoiced_ticks <= TUNER_HOLD_TICKS && ++g_app.unvoiced_ticks == TUNER_HOLD_TICKS)
    {
        mel_gui_set_text(g_app.tuner_note, S8("-"));
        mel_gui_set_text(g_app.tuner_cents, S8("listening..."));
    }
    return true;
}

static bool tuner_authorize(void)
{
    Mel_Future* auth = mel_audioin_authorize(mel_alloc_heap());
    if (auth == NULL)
        return false;
    while (!mel_future_resolved(auth))
        mel_thread_sleep(10 * 1000 * 1000);
    bool granted = mel_audioin_auth_is_granted(mel_audioin_future_auth(auth));
    mel_audioin_future_free(auth);
    return granted;
}

static void tuner_start_clicked(Mel_Gui_Handle h, void* user)
{
    MEL_UNUSED(h);
    MEL_UNUSED(user);

    if (g_app.cap != NULL)
    {
        mel_gui_set_text(g_app.tuner_status, S8("Status: already running"));
        return;
    }

    if (!tuner_authorize())
    {
        mel_gui_set_text(g_app.tuner_status, S8("Status: microphone access denied"));
        return;
    }

    Mel_AudioIn device = mel_audioin_default();
    if (!mel_audioin_alive(device))
    {
        mel_gui_set_text(g_app.tuner_status, S8("Status: no input device"));
        return;
    }

    Mel_AudioCapture_Open_Result opened = mel_audiocapture_open(mel_alloc_heap(), device,
                                                               (Mel_AudioCapture_Opt){
                                                                   .sample_rate = TUNER_SAMPLE_RATE,
                                                                   .channels = 1,
                                                                   .ring_capacity_frames = TUNER_SAMPLE_RATE / 4,
                                                               });
    if (mel_audiocapture_status_failed(opened.status))
    {
        mel_gui_set_text(g_app.tuner_status, S8("Status: open failed (mic permission?)"));
        return;
    }
    g_app.cap = opened.capture;

    g_app.window_fill = 0;
    g_app.unvoiced_ticks = 0;
    mel_gui_set_text(g_app.tuner_status, S8("Status: listening"));
    mel_gui_set_text(g_app.tuner_cents, S8("listening..."));

    if (g_app.tuner_timer == NULL)
        g_app.tuner_timer = mel_vat_tick_open(g_app.vat, mel_alloc_heap(), POLL_INTERVAL_NS, tuner_tick, NULL);
}

static void tuner_stop_clicked(Mel_Gui_Handle h, void* user)
{
    MEL_UNUSED(h);
    MEL_UNUSED(user);

    if (g_app.cap == NULL)
    {
        mel_gui_set_text(g_app.tuner_status, S8("Status: not running"));
        return;
    }

    if (g_app.tuner_timer != NULL)
    {
        mel_vat_tick_close(g_app.tuner_timer);
        g_app.tuner_timer = NULL;
    }
    mel_audiocapture_close(g_app.cap);
    g_app.cap = NULL;
    mel_gui_set_text(g_app.tuner_status, S8("Status: stopped"));
    mel_gui_set_text(g_app.tuner_note, S8("-"));
    mel_gui_set_text(g_app.tuner_cents, S8(""));
}

static void build_tuner_tab(Mel_Gui_Handle tab)
{
    mel_label_create(tab, .text = S8("Play a note; the nearest pitch and deviation appear below."), .x = 24, .y = 16, .w = 480, .h = 32);

    mel_button_create(tab, .text = S8("Start"), .x = 24, .y = 60, .w = 120, .h = 48, .pointer.on_click = tuner_start_clicked);
    mel_button_create(tab, .text = S8("Stop"), .x = 156, .y = 60, .w = 120, .h = 48, .pointer.on_click = tuner_stop_clicked);

    g_app.tuner_status = mel_label_create(tab, .text = S8("Status: idle"), .x = 24, .y = 124, .w = 480, .h = 30);
    g_app.tuner_note = mel_label_create(tab, .text = S8("-"), .x = 24, .y = 162, .w = 480, .h = 60);
    g_app.tuner_cents = mel_label_create(tab, .text = S8(""), .x = 24, .y = 228, .w = 480, .h = 30);
}

static void build_sightreading_tab(Mel_Gui_Handle tab)
{
    mel_label_create(tab, .text = S8("Sightreading"), .x = 24, .y = 16, .w = 480, .h = 32);
    mel_label_create(tab, .text = S8("Exercise generation arrives with the staff renderer."), .x = 24, .y = 56, .w = 480, .h = 30);
}

void build_companion(Mel_Gui_Handle frame, void* user)
{
    Mel_Vat* vat = (Mel_Vat*)user;
    memset(&g_app, 0, sizeof(g_app));
    g_app.vat = vat;

    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_audioin_init(alloc, mel_executor_inline());
    g_app.tuning = mel_tuning_western(alloc, mel_freq(440.0));
    g_app.western = mel_notation_western(alloc, &g_app.tuning);
    g_app.catalog = mel_chord_catalog_western(alloc);
    g_app.detector = mel_pitch_detector_make(alloc,
                                             (Mel_PitchDetector_Opt){
                                                 .sample_rate = TUNER_SAMPLE_RATE,
                                                 .window_size = TUNER_WINDOW,
                                                 .min_hz = 50.0,
                                                 .max_hz = 1500.0,
                                                 .threshold = 0.15f,
                                             });

    mel_gui_set_text(frame, S8("Music Companion"));

    Mel_Gui_Handle tabs = mel_tabview_create(frame, .x = 0, .y = 0, .w = 560, .h = 420);
    Mel_Gui_Handle chords = mel_tab_create(tabs, .title = S8("Chords"));
    Mel_Gui_Handle tuner = mel_tab_create(tabs, .title = S8("Tuner"));
    Mel_Gui_Handle sight = mel_tab_create(tabs, .title = S8("Sightreading"));

    build_chords_tab(chords);
    build_tuner_tab(tuner);
    build_sightreading_tab(sight);

    mel_tabview_select(tabs, 0);
}
