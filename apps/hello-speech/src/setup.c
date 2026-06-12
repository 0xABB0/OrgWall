#include <stdio.h>
#include <string.h>

#include <boot/boot.h>
#include <vat/vat.h>
#include <vat/tick.h>
#include <gui/gui.h>
#include <tts/tts.h>
#include <stt/stt.h>
#include <audioin/audioin.h>
#include <future/future.h>
#include <thread/mutex.h>
#include <collection/array.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <log/log.h>

#define HELLO_SPEECH_TICK_NS 50000000ll

typedef struct
{
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
    Mel_Vat_Tick*    tick;
    Mel_Mutex        lock;

    Mel_Array(Mel_Tts_Voice) voices;
    usize              voice_idx;
    Mel_Stt_Recognizer rec;
    Mel_Tts_Utterance  utt;
    Mel_Stt_Session    session;
    Mel_Future*        auth_future;

    Mel_Gui_Handle info_label;
    Mel_Gui_Handle voice_label;
    Mel_Gui_Handle text_field;
    Mel_Gui_Handle rate_slider;
    Mel_Gui_Handle tts_label;
    Mel_Gui_Handle auth_label;
    Mel_Gui_Handle listen_btn;
    Mel_Gui_Handle transcript_label;

    char tts_buf[160];
    bool tts_dirty;
    char transcript_buf[512];
    bool transcript_dirty;
    bool listen_ended;
} App;

static App g_app;

static void set_label(Mel_Gui_Handle h, const char* fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (!mel_gui_handle_is_none(h))
        mel_gui_set_text(h, str8_from_cstr(buf));
}

static void post_tts_status(const char* fmt, ...)
{
    mel_mutex_lock(&g_app.lock);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_app.tts_buf, sizeof g_app.tts_buf, fmt, ap);
    va_end(ap);
    g_app.tts_dirty = true;
    mel_mutex_unlock(&g_app.lock);
}

static void update_info(void) { set_label(g_app.info_label, "voices: %u   recognizers: %u   auth: %s", mel_tts_voice_count(), mel_stt_recognizer_count(), mel_stt_auth_name(mel_stt_authorization())); }

static void update_voice_label(void)
{
    if (g_app.voices.count == 0)
    {
        set_label(g_app.voice_label, "no voices on this host");
        return;
    }
    Mel_Tts_Voice                 v = g_app.voices.items[g_app.voice_idx];
    Mel_Tts_Voice_Describe_Result d = mel_tts_voice_describe(v);
    if (mel_tts_failed(d.status))
    {
        set_label(g_app.voice_label, "voice %zu/%zu: <dead>", g_app.voice_idx + 1, g_app.voices.count);
        return;
    }
    set_label(g_app.voice_label,
              "voice %zu/%zu: %.*s (%.*s)%s%s",
              g_app.voice_idx + 1,
              g_app.voices.count,
              (int)d.value.name.len,
              (const char*)d.value.name.data,
              (int)d.value.language.len,
              (const char*)d.value.language.data,
              d.value.caps.ranges ? " +ranges" : "",
              d.value.caps.can_pause ? " +pause" : "");
}

static void voices_reload(void)
{
    u32 n = mel_tts_voice_count();
    mel_array_clear(&g_app.voices);
    if (n > 0)
    {
        mel_array_reserve(&g_app.voices, n);
        g_app.voices.count = mel_tts_voice_list(g_app.voices.items, n);
    }
    if (g_app.voice_idx >= g_app.voices.count)
        g_app.voice_idx = 0;

    Mel_Stt_Recognizer recs[1];
    g_app.rec = mel_stt_recognizer_list(recs, 1) > 0 ? recs[0] : MEL_STT_RECOGNIZER_NULL;

    update_info();
    update_voice_label();
}

static void refresh_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_tts_refresh();
    mel_stt_refresh();
    voices_reload();
}

static void prev_voice_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.voices.count == 0)
        return;
    g_app.voice_idx = (g_app.voice_idx + g_app.voices.count - 1) % g_app.voices.count;
    update_voice_label();
}

static void next_voice_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.voices.count == 0)
        return;
    g_app.voice_idx = (g_app.voice_idx + 1) % g_app.voices.count;
    update_voice_label();
}

static void on_speak_done(Mel_Tts_Utterance u, Mel_Tts_Status status, void* user)
{
    (void)u;
    (void)user;
    post_tts_status("utterance resolved status=0x%x%s", status, (status & MEL_TTS_RESULT_ABORTED) ? " (aborted)" : "");
}

static void on_speak_range(Mel_Tts_Utterance u, Mel_Tts_Range range, void* user)
{
    (void)u;
    (void)user;
    post_tts_status("speaking word @ byte %zu (+%zu)", range.offset, range.length);
}

static void speak_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.voices.count == 0)
    {
        set_label(g_app.tts_label, "no voice to speak with");
        return;
    }
    char text[512];
    size n = mel_gui_get_text(g_app.text_field, text, sizeof text);
    if (n <= 0)
    {
        set_label(g_app.tts_label, "type some text first");
        return;
    }
    f32                  rate = (f32)mel_slider_value(g_app.rate_slider) / 100.0f;
    Mel_Tts_Voice        v = g_app.voices.items[g_app.voice_idx];
    Mel_Tts_Speak_Result r = mel_tts_speak(v, str8_from_parts((u8*)text, n), .rate = rate, .on_complete = on_speak_done, .on_range = on_speak_range);
    g_app.utt = r.value;
    set_label(g_app.tts_label, "SPEAK status=0x%x speaking=%d", r.status, (int)mel_tts_speaking(r.value));
}

static void pause_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    Mel_Tts_Status s = mel_tts_pause(g_app.utt);
    set_label(g_app.tts_label, "PAUSE status=0x%x paused=%d", s, (int)mel_tts_paused(g_app.utt));
}

static void resume_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    Mel_Tts_Status s = mel_tts_resume(g_app.utt);
    set_label(g_app.tts_label, "RESUME status=0x%x speaking=%d", s, (int)mel_tts_speaking(g_app.utt));
}

static void abort_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_tts_abort(g_app.utt);
    g_app.utt = MEL_TTS_UTTERANCE_NULL;
    set_label(g_app.tts_label, "ABORT");
}

static void authorize_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.auth_future)
    {
        set_label(g_app.auth_label, "authorization already pending");
        return;
    }
    g_app.auth_future = mel_stt_authorize(g_app.alloc);
    set_label(g_app.auth_label, "authorizing...");
}

static void on_result(Mel_Stt_Session s, const Mel_Stt_Result* result, void* user)
{
    (void)s;
    (void)user;
    mel_mutex_lock(&g_app.lock);
    usize n = (usize)result->text.len;
    if (n > sizeof g_app.transcript_buf - 32)
        n = sizeof g_app.transcript_buf - 32;
    char head[16];
    snprintf(head, sizeof head, result->final ? "[final %.2f] " : "[...] ", result->confidence);
    usize hl = strlen(head);
    memcpy(g_app.transcript_buf, head, hl);
    memcpy(g_app.transcript_buf + hl, result->text.data, n);
    g_app.transcript_buf[hl + n] = 0;
    g_app.transcript_dirty = true;
    mel_mutex_unlock(&g_app.lock);
}

static void on_listen_done(Mel_Stt_Session s, Mel_Stt_Status status, void* user)
{
    (void)s;
    (void)user;
    mel_mutex_lock(&g_app.lock);
    usize len = strlen(g_app.transcript_buf);
    snprintf(g_app.transcript_buf + len, sizeof g_app.transcript_buf - len, "  [session done 0x%x]", status);
    g_app.transcript_dirty = true;
    g_app.listen_ended = true;
    mel_mutex_unlock(&g_app.lock);
}

static void listen_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (mel_stt_listening(g_app.session))
    {
        Mel_Stt_Status s = mel_stt_stop(g_app.session);
        set_label(g_app.transcript_label, "STOP status=0x%x", s);
        return;
    }
    if (!mel_stt_recognizer_alive(g_app.rec))
    {
        set_label(g_app.transcript_label, "no recognizer on this host");
        return;
    }
    Mel_Stt_Listen_Result r = mel_stt_listen(g_app.rec, .partials = true, .on_result = on_result, .on_complete = on_listen_done);
    g_app.session = r.value;
    if (mel_stt_failed(r.status))
    {
        set_label(g_app.transcript_label, "LISTEN failed status=0x%x (authorize first?)", r.status);
        return;
    }
    set_label(g_app.transcript_label, "listening... status=0x%x", r.status);
    mel_gui_set_text(g_app.listen_btn, S8("Stop listening"));
}

static void listen_abort_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_stt_abort(g_app.session);
    g_app.session = MEL_STT_SESSION_NULL;
}

static bool on_tick(void* user)
{
    (void)user;
    mel_mutex_lock(&g_app.lock);
    bool tts_dirty = g_app.tts_dirty;
    bool transcript_dirty = g_app.transcript_dirty;
    bool listen_ended = g_app.listen_ended;
    char tts_buf[sizeof g_app.tts_buf];
    char transcript_buf[sizeof g_app.transcript_buf];
    if (tts_dirty)
        memcpy(tts_buf, g_app.tts_buf, sizeof tts_buf);
    if (transcript_dirty)
        memcpy(transcript_buf, g_app.transcript_buf, sizeof transcript_buf);
    g_app.tts_dirty = false;
    g_app.transcript_dirty = false;
    g_app.listen_ended = false;
    mel_mutex_unlock(&g_app.lock);

    if (tts_dirty)
        set_label(g_app.tts_label, "%s", tts_buf);
    if (transcript_dirty)
        set_label(g_app.transcript_label, "%s", transcript_buf);
    if (listen_ended)
    {
        g_app.session = MEL_STT_SESSION_NULL;
        mel_gui_set_text(g_app.listen_btn, S8("Listen"));
    }
    if (g_app.auth_future && mel_future_resolved(g_app.auth_future))
    {
        const mel_stt_auth* a = mel_stt_future_auth(g_app.auth_future);
        mel_stt_future_free(g_app.auth_future);
        g_app.auth_future = NULL;
        set_label(g_app.auth_label, "auth: %s", mel_stt_auth_name(a));
        update_info();
    }
    return true;
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Speech"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    g_app.info_label = mel_label_create(frame, .text = S8(""), .layoutable = { .preferred_h = 24 });

    Mel_Gui_Handle voice_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 40 });
    mel_button_create(voice_row, .text = S8("<"), .pointer.on_click = prev_voice_clicked, .layoutable = { .preferred_w = 44 });
    g_app.voice_label = mel_label_create(voice_row, .text = S8(""), .layoutable = { .weight = 1 });
    mel_button_create(voice_row, .text = S8(">"), .pointer.on_click = next_voice_clicked, .layoutable = { .preferred_w = 44 });
    mel_button_create(voice_row, .text = S8("Refresh"), .pointer.on_click = refresh_clicked, .layoutable = { .preferred_w = 90 });

    g_app.text_field = mel_textfield_create(frame, .text = S8("Hello from Melody! This is the tts module talking."), .layoutable = { .preferred_h = 36 });
    g_app.rate_slider = mel_slider_create(frame, .min_value = 50, .max_value = 200, .value = 100, .layoutable = { .preferred_h = 32 });

    Mel_Gui_Handle tts_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 44 });
    mel_button_create(tts_row, .text = S8("Speak"), .pointer.on_click = speak_clicked, .layoutable = { .weight = 1 });
    mel_button_create(tts_row, .text = S8("Pause"), .pointer.on_click = pause_clicked, .layoutable = { .weight = 1 });
    mel_button_create(tts_row, .text = S8("Resume"), .pointer.on_click = resume_clicked, .layoutable = { .weight = 1 });
    mel_button_create(tts_row, .text = S8("Abort"), .pointer.on_click = abort_clicked, .layoutable = { .weight = 1 });

    g_app.tts_label = mel_label_create(frame, .text = S8("pick a voice, tap Speak"), .layoutable = { .preferred_h = 28 });

    Mel_Gui_Handle auth_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 40 });
    mel_button_create(auth_row, .text = S8("Authorize mic"), .pointer.on_click = authorize_clicked, .layoutable = { .preferred_w = 140 });
    g_app.auth_label = mel_label_create(auth_row, .text = S8("auth: ?"), .layoutable = { .weight = 1 });

    Mel_Gui_Handle stt_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 44 });
    g_app.listen_btn = mel_button_create(stt_row, .text = S8("Listen"), .pointer.on_click = listen_clicked, .layoutable = { .weight = 2 });
    mel_button_create(stt_row, .text = S8("Abort listen"), .pointer.on_click = listen_abort_clicked, .layoutable = { .weight = 1 });

    g_app.transcript_label = mel_label_create(frame, .text = S8("transcript appears here"), .layoutable = { .preferred_h = 64 });

    voices_reload();
    set_label(g_app.auth_label, "auth: %s", mel_stt_auth_name(mel_stt_authorization()));
}

void mel_app_setup(Mel_Vat* root)
{
    g_app.vat = root;
    g_app.alloc = mel_alloc_heap();
    mel_mutex_init(&g_app.lock, MEL_MUTEX_PLAIN);
    mel_array_init(&g_app.voices, g_app.alloc);

    mel_gui_init(root);
    mel_audioin_init(g_app.alloc, NULL);
    mel_tts_init(g_app.alloc);
    mel_stt_init(g_app.alloc);
    mel_log_info("hello-speech", "voices=%u recognizers=%u", mel_tts_voice_count(), mel_stt_recognizer_count());

    g_app.tick = mel_vat_tick_open(root, g_app.alloc, HELLO_SPEECH_TICK_NS, on_tick, NULL);

    mel_app_register_screen(S8("main"), .build = build_main);
    mel_app_present(S8("main"), NULL);
}
