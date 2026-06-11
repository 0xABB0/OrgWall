#include <speech/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <thread/thread.h>
#include <thread/mutex.h>
#include <thread/cond.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPD_EVENT_BEGIN  701u
#define SPD_EVENT_END    702u
#define SPD_EVENT_CANCEL 703u
#define SPD_EVENT_PAUSE  704u
#define SPD_EVENT_RESUME 705u

typedef struct
{
    u64             token;
    u32             msg_id;
    Mel_Speech_Sink sink;
} Spd_Job;

typedef Mel_Array(char) Spd_Buf;

typedef struct
{
    const Mel_Alloc* alloc;
    int              fd;
    bool             connected;
    bool             probed;
    bool             run;
    Mel_Thread       reader;
    Mel_Mutex        lock;
    Mel_Cond         resp_cv;

    Spd_Buf line;
    Spd_Buf resp_args;
    Spd_Buf event_args;
    u32     resp_code;
    bool    resp_ready;

    Mel_Array(Spd_Job) jobs;
    Mel_Array(Mel_Speech_Voice_Raw) voices;
    Mel_Array(str8) strings;
} Spd;

static Spd g_spd;

static str8 spd_intern(const char* utf8, usize len)
{
    u8* data = (u8*)mel_alloc(g_spd.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(&g_spd.strings, s);
    return s;
}

static void spd_strings_clear(void)
{
    for (usize i = 0; i < g_spd.strings.count; i++)
        if (g_spd.strings.items[i].data)
            mel_dealloc(g_spd.alloc, g_spd.strings.items[i].data);
    mel_array_clear(&g_spd.strings);
}

static void spd_buf_append(Spd_Buf* buf, const char* data, usize len)
{
    for (usize i = 0; i < len; i++)
        mel_array_push(buf, data[i]);
}

static Spd_Job* spd_job_by_msg(u32 msg_id)
{
    for (usize i = 0; i < g_spd.jobs.count; i++)
        if (g_spd.jobs.items[i].msg_id == msg_id)
            return &g_spd.jobs.items[i];
    return NULL;
}

static void spd_job_remove(Spd_Job* job)
{
    usize idx = (usize)(job - g_spd.jobs.items);
    g_spd.jobs.items[idx] = g_spd.jobs.items[g_spd.jobs.count - 1];
    g_spd.jobs.count--;
}

static void spd_dispatch_event(u32 code)
{
    if (code != SPD_EVENT_END && code != SPD_EVENT_CANCEL)
    {
        mel_array_clear(&g_spd.event_args);
        return;
    }
    mel_array_push(&g_spd.event_args, '\0');
    u32 msg_id = (u32)strtoul(g_spd.event_args.items, NULL, 10);
    mel_array_clear(&g_spd.event_args);

    Spd_Job* job = spd_job_by_msg(msg_id);
    if (!job)
        return;
    Mel_Speech_Sink sink = job->sink;
    spd_job_remove(job);
    Mel_Speech_Status status = code == SPD_EVENT_END ? MEL_SPEECH_OK : (MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED);
    mel_mutex_unlock(&g_spd.lock);
    if (sink.on_speak_done)
        sink.on_speak_done(sink.token, status);
    mel_mutex_lock(&g_spd.lock);
}

static void spd_connection_lost(void)
{
    while (g_spd.jobs.count > 0)
    {
        Spd_Job job = g_spd.jobs.items[g_spd.jobs.count - 1];
        g_spd.jobs.count--;
        mel_mutex_unlock(&g_spd.lock);
        if (job.sink.on_speak_done)
            job.sink.on_speak_done(job.sink.token, MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_LOST);
        mel_mutex_lock(&g_spd.lock);
    }
    g_spd.connected = false;
    g_spd.resp_code = 0;
    g_spd.resp_ready = true;
    mel_cond_broadcast(&g_spd.resp_cv);
}

static void spd_handle_line(const char* line, usize len)
{
    if (len < 4)
        return;
    u32         code = (u32)strtoul((char[]){ line[0], line[1], line[2], 0 }, NULL, 10);
    bool        cont = line[3] == '-';
    const char* arg = line + 4;
    usize       arg_len = len - 4;

    if (code >= 700)
    {
        if (cont)
        {
            if (g_spd.event_args.count == 0)
                spd_buf_append(&g_spd.event_args, arg, arg_len);
        }
        else
            spd_dispatch_event(code);
        return;
    }

    if (cont)
    {
        if (g_spd.resp_args.count > 0)
            mel_array_push(&g_spd.resp_args, '\n');
        spd_buf_append(&g_spd.resp_args, arg, arg_len);
        return;
    }
    g_spd.resp_code = code;
    g_spd.resp_ready = true;
    mel_cond_broadcast(&g_spd.resp_cv);
}

static int spd_reader_main(void* user)
{
    MEL_UNUSED(user);
    char chunk[256];
    mel_mutex_lock(&g_spd.lock);
    while (g_spd.run)
    {
        mel_mutex_unlock(&g_spd.lock);
        ssize_t n = read(g_spd.fd, chunk, sizeof chunk);
        mel_mutex_lock(&g_spd.lock);
        if (n <= 0)
        {
            if (g_spd.run)
                spd_connection_lost();
            break;
        }
        for (ssize_t i = 0; i < n; i++)
        {
            char c = chunk[i];
            if (c == '\n')
            {
                usize len = g_spd.line.count;
                while (len > 0 && g_spd.line.items[len - 1] == '\r')
                    len--;
                spd_handle_line(g_spd.line.items, len);
                mel_array_clear(&g_spd.line);
            }
            else
                mel_array_push(&g_spd.line, c);
        }
    }
    mel_mutex_unlock(&g_spd.lock);
    return 0;
}

static bool spd_send(const char* data, usize len)
{
    while (len > 0)
    {
        ssize_t n = write(g_spd.fd, data, len);
        if (n <= 0)
            return false;
        data += (usize)n;
        len -= (usize)n;
    }
    return true;
}

static u32 spd_wait_response(Spd_Buf* args_out)
{
    while (!g_spd.resp_ready)
        mel_cond_wait(&g_spd.resp_cv, &g_spd.lock);
    g_spd.resp_ready = false;
    u32 code = g_spd.resp_code;
    if (args_out)
    {
        mel_array_clear(args_out);
        spd_buf_append(args_out, g_spd.resp_args.items, g_spd.resp_args.count);
    }
    mel_array_clear(&g_spd.resp_args);
    return code;
}

static u32 spd_command(const char* cmd, Spd_Buf* args_out)
{
    if (!g_spd.connected)
        return 0;
    if (!spd_send(cmd, strlen(cmd)) || !spd_send("\r\n", 2))
    {
        spd_connection_lost();
        return 0;
    }
    return spd_wait_response(args_out);
}

static bool spd_ok(u32 code) { return code >= 200 && code < 300; }

static bool spd_connect_locked(void)
{
    if (g_spd.connected)
        return true;
    if (g_spd.probed)
        return false;
    g_spd.probed = true;

    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime)
    {
        mel_log_info("speech", "spd: XDG_RUNTIME_DIR unset; speech-dispatcher unavailable");
        return false;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    int written = snprintf(addr.sun_path, sizeof addr.sun_path, "%s/speech-dispatcher/speechd.sock", runtime);
    if (written < 0 || (usize)written >= sizeof addr.sun_path)
        return false;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    if (connect(fd, (struct sockaddr*)&addr, sizeof addr) != 0)
    {
        close(fd);
        mel_log_info("speech", "spd: speech-dispatcher not running at %s", addr.sun_path);
        return false;
    }

    g_spd.fd = fd;
    g_spd.connected = true;
    g_spd.run = true;
    mel_array_init(&g_spd.line, g_spd.alloc);
    mel_array_init(&g_spd.resp_args, g_spd.alloc);
    mel_array_init(&g_spd.event_args, g_spd.alloc);
    mel_array_init(&g_spd.jobs, g_spd.alloc);
    if (!mel_thread_spawn(&g_spd.reader, spd_reader_main, NULL, .name = "mel-spd"))
    {
        g_spd.connected = false;
        g_spd.run = false;
        close(fd);
        return false;
    }

    if (!spd_ok(spd_command("SET self CLIENT_NAME user:melody:speech", NULL)))
        mel_log_warn("speech", "spd: CLIENT_NAME rejected");
    if (!spd_ok(spd_command("SET self NOTIFICATION ALL on", NULL)))
        mel_log_warn("speech", "spd: notifications rejected; completion will not fire");
    return g_spd.connected;
}

static void spd_voices_rebuild_locked(void)
{
    Spd_Buf args;
    mel_array_init(&args, g_spd.alloc);
    u32 code = spd_command("LIST SYNTHESIS_VOICES", &args);
    mel_array_clear(&g_spd.voices);
    spd_strings_clear();
    if (!spd_ok(code))
    {
        mel_array_free(&args);
        return;
    }
    usize start = 0;
    for (usize i = 0; i <= args.count; i++)
    {
        if (i < args.count && args.items[i] != '\n')
            continue;
        usize len = i - start;
        if (len > 0)
        {
            const char* rec = &args.items[start];
            usize       name_len = len;
            usize       lang_off = len;
            for (usize k = 0; k < len; k++)
                if (rec[k] == '\t')
                {
                    name_len = k;
                    lang_off = k + 1;
                    break;
                }
            usize lang_len = len - lang_off;
            for (usize k = lang_off; k < len; k++)
                if (rec[k] == '\t')
                {
                    lang_len = k - lang_off;
                    break;
                }
            str8 name = spd_intern(rec, name_len);
            str8 lang = lang_off < len ? spd_intern(rec + lang_off, lang_len) : (str8){ 0 };
            Mel_Speech_Voice_Raw raw = {
                .stable_id = str8_hash(name),
                .name = name,
                .language = lang,
                .caps = {
                    .rate = true,
                    .rate_min = 0.5f,
                    .rate_max = 2.0f,
                    .pitch = true,
                    .volume = true,
                    .ranges = false,
                    .can_pause = true,
                },
            };
            mel_array_push(&g_spd.voices, raw);
        }
        start = i + 1;
    }
    mel_array_free(&args);
}

static u32 spd_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (g_spd.alloc == NULL)
    {
        g_spd.alloc = alloc;
        mel_mutex_init(&g_spd.lock, MEL_MUTEX_PLAIN);
        mel_cond_init(&g_spd.resp_cv);
        mel_array_init(&g_spd.voices, alloc);
        mel_array_init(&g_spd.strings, alloc);
    }
    mel_mutex_lock(&g_spd.lock);
    if (spd_connect_locked())
        spd_voices_rebuild_locked();
    u32 total = (u32)g_spd.voices.count;
    u32 n = total < cap ? total : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = g_spd.voices.items[i];
    mel_mutex_unlock(&g_spd.lock);
    return total;
}

static const Mel_Speech_Voice_Raw* spd_voice_by_id_locked(u64 stable_id)
{
    for (usize i = 0; i < g_spd.voices.count; i++)
        if (g_spd.voices.items[i].stable_id == stable_id)
            return &g_spd.voices.items[i];
    return NULL;
}

static i32 spd_percent(f32 multiplier)
{
    f32 pct = (multiplier - 1.0f) * 100.0f;
    if (pct < -100.0f)
        pct = -100.0f;
    if (pct > 100.0f)
        pct = 100.0f;
    return (i32)pct;
}

static Mel_Speech_Status spd_speak(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    mel_mutex_lock(&g_spd.lock);
    if (!g_spd.connected)
    {
        mel_mutex_unlock(&g_spd.lock);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_LOST;
    }
    const Mel_Speech_Voice_Raw* voice = spd_voice_by_id_locked(stable_id);
    if (!voice)
    {
        mel_mutex_unlock(&g_spd.lock);
        mel_log_error("speech", "spd speak: voice %llu not found", (unsigned long long)stable_id);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
    }

    char cmd[512];
    snprintf(cmd, sizeof cmd, "SET self SYNTHESIS_VOICE %.*s", (int)voice->name.len, voice->name.data);
    spd_command(cmd, NULL);
    snprintf(cmd, sizeof cmd, "SET self RATE %d", lowered->rate > 0.0f ? spd_percent(lowered->rate) : 0);
    spd_command(cmd, NULL);
    snprintf(cmd, sizeof cmd, "SET self PITCH %d", lowered->pitch > 0.0f ? spd_percent(lowered->pitch) : 0);
    spd_command(cmd, NULL);
    snprintf(cmd, sizeof cmd, "SET self VOLUME %d", lowered->volume > 0.0f ? (i32)(lowered->volume * 200.0f - 100.0f) : 100);
    spd_command(cmd, NULL);

    if (!spd_ok(spd_command("SPEAK", NULL)))
    {
        mel_mutex_unlock(&g_spd.lock);
        mel_log_error("speech", "spd speak: SPEAK rejected");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }

    Spd_Buf data;
    mel_array_init(&data, g_spd.alloc);
    bool at_line_start = true;
    for (size i = 0; i < lowered->text.len; i++)
    {
        char c = (char)lowered->text.data[i];
        if (at_line_start && c == '.')
            mel_array_push(&data, '.');
        mel_array_push(&data, c);
        at_line_start = c == '\n';
    }
    spd_buf_append(&data, "\r\n.", 3);
    mel_array_push(&data, '\0');

    Spd_Buf args;
    mel_array_init(&args, g_spd.alloc);
    bool sent = spd_send(data.items, strlen(data.items)) && spd_send("\r\n", 2);
    mel_array_free(&data);
    if (!sent)
    {
        spd_connection_lost();
        mel_array_free(&args);
        mel_mutex_unlock(&g_spd.lock);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_LOST;
    }
    u32 code = spd_wait_response(&args);
    if (!spd_ok(code))
    {
        mel_array_free(&args);
        mel_mutex_unlock(&g_spd.lock);
        mel_log_error("speech", "spd speak: message rejected with code %u", code);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    mel_array_push(&args, '\0');
    u32 msg_id = (u32)strtoul(args.items, NULL, 10);
    mel_array_free(&args);

    Spd_Job job = { .token = token, .msg_id = msg_id, .sink = sink };
    mel_array_push(&g_spd.jobs, job);
    mel_mutex_unlock(&g_spd.lock);
    return MEL_SPEECH_OK;
}

static void spd_simple(const char* cmd)
{
    mel_mutex_lock(&g_spd.lock);
    if (g_spd.connected)
        spd_command(cmd, NULL);
    mel_mutex_unlock(&g_spd.lock);
}

static void spd_speak_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    spd_simple("PAUSE self");
}

static void spd_speak_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    spd_simple("RESUME self");
}

static void spd_speak_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    mel_mutex_lock(&g_spd.lock);
    for (usize i = 0; i < g_spd.jobs.count; i++)
    {
        if (g_spd.jobs.items[i].token == token)
        {
            spd_job_remove(&g_spd.jobs.items[i]);
            break;
        }
    }
    if (g_spd.connected)
        spd_command("CANCEL self", NULL);
    mel_mutex_unlock(&g_spd.lock);
}

static const mel_speech_auth* spd_authorization(void* user)
{
    MEL_UNUSED(user);
    return &mel_speech_auth_granted;
}

static void spd_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_spd.alloc == NULL)
        return;
    mel_mutex_lock(&g_spd.lock);
    bool was_connected = g_spd.connected;
    g_spd.run = false;
    g_spd.connected = false;
    mel_mutex_unlock(&g_spd.lock);
    if (was_connected)
    {
        shutdown(g_spd.fd, SHUT_RDWR);
        mel_thread_join(&g_spd.reader, NULL);
        close(g_spd.fd);
        mel_array_free(&g_spd.line);
        mel_array_free(&g_spd.resp_args);
        mel_array_free(&g_spd.event_args);
        mel_array_free(&g_spd.jobs);
    }
    spd_strings_clear();
    mel_array_free(&g_spd.strings);
    mel_array_free(&g_spd.voices);
    mel_cond_destroy(&g_spd.resp_cv);
    mel_mutex_destroy(&g_spd.lock);
    memset(&g_spd, 0, sizeof g_spd);
}

void mel_speech__register_host_providers(void)
{
    static const Mel_Speech_Provider_Desc desc = {
        .name = "linux-speechd",
        .enumerate_voices = spd_enumerate_voices,
        .speak = spd_speak,
        .speak_pause = spd_speak_pause,
        .speak_resume = spd_speak_resume,
        .speak_abort = spd_speak_abort,
        .authorization = spd_authorization,
        .shutdown = spd_shutdown,
    };
    mel_speech_provider_register(&desc);
}
