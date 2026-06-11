#include "job.coro.h"
#include "lab.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <compress/zip.h>
#include <dialog/dialog.h>
#include <executor/executor.h>
#include <future/future.h>
#include <gui/gui.h>
#include <gui/controls/button.h>
#include <gui/controls/label.h>
#include <gui/controls/panel.h>
#include <gui/controls/slider.h>
#include <io/file.h>
#include <log/log.h>
#include <string/str8.h>
#include <time/nano.h>
#include <vat/tick.h>
#include <vat/vat.h>

#include <stdio.h>
#include <string.h>

#define LAB_TICK_NS      ((i64)16 * 1000 * 1000)
#define LAB_TICK_BUDGET  ((mel_nanosec)8 * 1000 * 1000)
#define LAB_SAMPLE_LEN   ((usize)2 * 1024 * 1024)
#define LAB_SNIFF_LEN    ((size)18)
#define LAB_PROGRESS_MAX 1000

typedef struct
{
    Mel_Vat*         vat;
    Mel_Executor*    exec;
    const Mel_Alloc* alloc;

    Mel_Gui_Handle  open_btn;
    Mel_Gui_Handle  sample_btn;
    Mel_Gui_Handle  file_label;
    Mel_Gui_Handle* codec_btns;
    Mel_Gui_Handle  level_label;
    Mel_Gui_Handle  level_slider;
    Mel_Gui_Handle  compress_btn;
    Mel_Gui_Handle  decompress_btn;
    Mel_Gui_Handle  race_btn;
    Mel_Gui_Handle  progress_bar;
    Mel_Gui_Handle  status_label;
    Mel_Gui_Handle* result_labels;
    Mel_Gui_Handle  summary_label;
    Mel_Gui_Handle  output_label;
    Mel_Gui_Handle  save_btn;

    usize codec_count;
    usize selected;

    str8  input;
    str8  input_name;
    char* input_path;
    bool  have_input;

    u8*   output;
    usize output_len;
    bool  have_output;
    bool  output_decompressed;
    str8  output_codec_id;

    Lab_Job                     job;
    Lab_Race                    race;
    Mel_Coro_Frame_lab_pump     pump_frame;
    Mel_Coro_Frame_lab_race_run race_frame;
    bool                        job_live;
    bool                        race_live;
    bool                        busy;

    Mel_Vat_Tick* tick;
    Mel_Task      stage_task;
    Mel_Future*   pending;
    char*         save_path;
} Lab_Ui;

static Lab_Ui g;

static void fmt_bytes(char* buf, usize cap, u64 n)
{
    if (n >= 1024u * 1024u * 1024u)
        snprintf(buf, cap, "%.2f GB", (double)n / (1024.0 * 1024.0 * 1024.0));
    else if (n >= 1024u * 1024u)
        snprintf(buf, cap, "%.2f MB", (double)n / (1024.0 * 1024.0));
    else if (n >= 1024u)
        snprintf(buf, cap, "%.1f KB", (double)n / 1024.0);
    else
        snprintf(buf, cap, "%llu B", (unsigned long long)n);
}

static void set_label(Mel_Gui_Handle h, const char* text)
{
    if (!mel_gui_handle_is_none(h))
        mel_gui_set_text(h, str8_from_cstr(text));
}

static void set_status(const char* text) { set_label(g.status_label, text); }

static u32 level_from_slider(const Mel_Compress_Codec* c)
{
    i32 v = mel_slider_value(g.level_slider);
    return c->level_min + (u32)(((c->level_max - c->level_min) * (u32)v + 50) / 100);
}

static void update_level_label(void)
{
    const Mel_Compress_Codec* c = mel_compress_at(g.selected);
    char                      buf[128];
    snprintf(buf, sizeof buf, "level %u  (%.*s: %u..%u, default %u)", level_from_slider(c), (int)c->id.len, c->id.data, c->level_min, c->level_max, c->level_default);
    set_label(g.level_label, buf);
}

static void on_level_changed(Mel_Gui_Handle h, i32 value, void* user)
{
    (void)h;
    (void)value;
    (void)user;
    update_level_label();
}

static void select_codec(usize index)
{
    g.selected = index;
    for (usize i = 0; i < g.codec_count; i++)
    {
        const Mel_Compress_Codec* c = mel_compress_at(i);
        char                      buf[64];
        snprintf(buf, sizeof buf, "%s%.*s", i == index ? "● " : "", (int)c->id.len, c->id.data);
        set_label(g.codec_btns[i], buf);
    }
    const Mel_Compress_Codec* c = mel_compress_at(index);
    i32                       pct = c->level_max == c->level_min ? 0 : (i32)(((c->level_default - c->level_min) * 100) / (c->level_max - c->level_min));
    mel_slider_set_value(g.level_slider, pct);
    update_level_label();
}

static void on_codec_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    if (g.busy)
        return;
    select_codec((usize)user);
}

static void controls_set_busy(bool busy)
{
    g.busy = busy;
    mel_gui_set_enabled(g.open_btn, !busy);
    mel_gui_set_enabled(g.sample_btn, !busy);
    mel_gui_set_enabled(g.compress_btn, !busy);
    mel_gui_set_enabled(g.decompress_btn, !busy);
    mel_gui_set_enabled(g.race_btn, !busy);
    mel_gui_set_enabled(g.level_slider, !busy);
    mel_gui_set_enabled(g.save_btn, !busy && g.have_output);
    for (usize i = 0; i < g.codec_count; i++)
        mel_gui_set_enabled(g.codec_btns[i], !busy);
}

static void update_file_label(void)
{
    char nbuf[64];
    char buf[512];
    if (!g.have_input)
    {
        set_label(g.file_label, "no input loaded — open a file or generate sample data");
        return;
    }
    fmt_bytes(nbuf, sizeof nbuf, (u64)g.input.len);
    snprintf(buf, sizeof buf, "%.*s — %s", (int)g.input_name.len, g.input_name.data, nbuf);
    set_label(g.file_label, buf);
}

static void drop_output(void)
{
    if (g.output)
        mel_dealloc(g.alloc, g.output);
    g.output = NULL;
    g.output_len = 0;
    g.have_output = false;
    set_label(g.output_label, "no output yet");
}

static void set_input(u8* data, usize len, str8 name)
{
    if (g.input.data)
        mel_dealloc(g.alloc, g.input.data);
    if (g.input_name.data)
        mel_dealloc(g.alloc, g.input_name.data);
    g.input = str8_from_parts(data, (size)len);
    g.input_name = str8_dup_alloc(name, g.alloc);
    g.have_input = true;
    drop_output();
    update_file_label();
}

static void update_progress(usize done, usize total, mel_nanosec elapsed)
{
    char nbuf[64];
    char buf[256];
    i32  bar = total ? (i32)((done * LAB_PROGRESS_MAX) / total) : LAB_PROGRESS_MAX;
    mel_slider_set_value(g.progress_bar, bar);
    double sec = (double)elapsed / 1e9;
    double mbs = sec > 0.0001 ? ((double)done / 1e6) / sec : 0.0;
    fmt_bytes(nbuf, sizeof nbuf, (u64)done);
    if (g.race_live)
    {
        const Mel_Compress_Codec* c = mel_compress_at((usize)g.race.current);
        snprintf(buf, sizeof buf, "racing %.*s (%d/%u) — %s in, %.1f MB/s", (int)c->id.len, c->id.data, g.race.current + 1, (u32)g.race.count, nbuf, mbs);
    }
    else
        snprintf(buf, sizeof buf, "%s — %s in, %.1f MB/s", g.job.decompress ? "decompressing" : "compressing", nbuf, mbs);
    set_status(buf);
}

static void update_race_labels(void)
{
    for (usize i = 0; i < g.race.count; i++)
    {
        Lab_Race_Result* r = &g.race.results[i];
        char             buf[256];
        if (!r->done)
        {
            const Mel_Compress_Codec* c = mel_compress_at(i);
            snprintf(buf, sizeof buf, "%-8.*s …", (int)c->id.len, c->id.data);
        }
        else if (r->failed)
            snprintf(buf, sizeof buf, "%-8.*s failed", (int)r->id.len, r->id.data);
        else
        {
            char   ibuf[64], obuf[64];
            double sec = (double)r->ns / 1e9;
            double mbs = sec > 0.0001 ? ((double)r->in_len / 1e6) / sec : 0.0;
            double pct = r->in_len ? 100.0 * (double)r->out_len / (double)r->in_len : 0.0;
            fmt_bytes(ibuf, sizeof ibuf, r->in_len);
            fmt_bytes(obuf, sizeof obuf, r->out_len);
            snprintf(buf, sizeof buf, "%-8.*s %s → %s   %5.1f%%   %8.1f MB/s   %.2fs", (int)r->id.len, r->id.data, ibuf, obuf, pct, mbs, sec);
        }
        set_label(g.result_labels[i], buf);
    }
}

static void start_tick(void) { mel_vat_tick_set_interval(g.tick, LAB_TICK_NS); }

static void finish_single(void)
{
    lab_job_close(&g.job);
    g.job_live = false;
    mel_slider_set_value(g.progress_bar, LAB_PROGRESS_MAX);

    if (g.job.failed)
    {
        char buf[160];
        snprintf(buf,
                 sizeof buf,
                 "%s failed — status 0x%x%s",
                 g.job.decompress ? "decompress" : "compress",
                 g.job.status,
                 (g.job.status & MEL_COMPRESS_UNKNOWN_FORMAT) ? " (unknown format)"
                 : (g.job.status & MEL_COMPRESS_TRUNCATED)    ? " (truncated)"
                 : (g.job.status & MEL_COMPRESS_CORRUPT)      ? " (corrupt)"
                                                              : "");
        set_status(buf);
        if (g.job.out)
            mel_dealloc(g.alloc, g.job.out);
        g.job.out = NULL;
        controls_set_busy(false);
        return;
    }

    g.output = g.job.out;
    g.output_len = g.job.out_len;
    g.have_output = true;
    g.output_decompressed = g.job.decompress;
    g.output_codec_id = g.job.codec->id;
    g.job.out = NULL;

    char   ibuf[64], obuf[64], buf[320];
    double sec = (double)(g.job.t_end - g.job.t_begin) / 1e9;
    double mbs = sec > 0.0001 ? ((double)g.job.in.len / 1e6) / sec : 0.0;
    fmt_bytes(ibuf, sizeof ibuf, (u64)g.job.in.len);
    fmt_bytes(obuf, sizeof obuf, (u64)g.output_len);
    if (g.job.decompress)
        snprintf(buf, sizeof buf, "decompressed (%.*s): %s → %s in %.2fs (%.1f MB/s)", (int)g.job.codec->id.len, g.job.codec->id.data, ibuf, obuf, sec, mbs);
    else
    {
        double pct = g.job.in.len ? 100.0 * (double)g.output_len / (double)g.job.in.len : 0.0;
        snprintf(buf, sizeof buf, "compressed (%.*s, level %u): %s → %s = %.1f%% in %.2fs (%.1f MB/s)", (int)g.job.codec->id.len, g.job.codec->id.data, g.job.level, ibuf, obuf, pct, sec, mbs);
    }
    set_status(buf);
    set_label(g.output_label, buf);
    controls_set_busy(false);
}

static void finish_race(void)
{
    g.race_live = false;
    mel_slider_set_value(g.progress_bar, LAB_PROGRESS_MAX);
    update_race_labels();

    usize best_ratio = 0;
    usize fastest = 0;
    bool  any = false;
    for (usize i = 0; i < g.race.count; i++)
    {
        Lab_Race_Result* r = &g.race.results[i];
        if (!r->done || r->failed)
            continue;
        if (!any)
        {
            best_ratio = i;
            fastest = i;
            any = true;
            continue;
        }
        if (r->out_len < g.race.results[best_ratio].out_len)
            best_ratio = i;
        if (r->ns < g.race.results[fastest].ns)
            fastest = i;
    }
    if (any)
    {
        Lab_Race_Result* b = &g.race.results[best_ratio];
        Lab_Race_Result* f = &g.race.results[fastest];
        char             buf[200];
        snprintf(buf, sizeof buf, "race done — best ratio: %.*s, fastest: %.*s", (int)b->id.len, b->id.data, (int)f->id.len, f->id.data);
        set_status(buf);
        set_label(g.summary_label, buf);
    }
    else
        set_status("race done — every codec failed");
    controls_set_busy(false);
}

static bool lab_tick(void* user)
{
    (void)user;
    mel_nanosec t0 = mel_nanos_since_unspecified_epoch();

    if (g.job_live)
    {
        i64  y = 0;
        bool suspended = true;
        while (suspended && mel_nanos_since_unspecified_epoch() - t0 < LAB_TICK_BUDGET)
            suspended = lab_pump__resume(&g.pump_frame, &y);
        if (suspended)
        {
            update_progress(g.job.in_consumed, (usize)g.job.in.len, mel_nanos_since_unspecified_epoch() - g.job.t_begin);
            return true;
        }
        finish_single();
        return false;
    }

    if (g.race_live)
    {
        i64  y = 0;
        bool suspended = true;
        while (suspended && mel_nanos_since_unspecified_epoch() - t0 < LAB_TICK_BUDGET)
            suspended = lab_race_run__resume(&g.race_frame, &y);
        if (suspended)
        {
            update_progress(g.race.job.in_consumed, (usize)g.race.input.len, mel_nanos_since_unspecified_epoch() - g.race.job.t_begin);
            update_race_labels();
            return true;
        }
        finish_race();
        return false;
    }

    return false;
}

static const Mel_Compress_Codec* resolve_decompress_codec(void)
{
    size len = g.input.len < LAB_SNIFF_LEN ? g.input.len : LAB_SNIFF_LEN;
    str8 head = str8_prefix(g.input, len);

    const Mel_Compress_Codec* c = mel_compress_sniff(head);
    if (c)
        return c;

    size dot = str8_rfind(g.input_name, S8("."));
    if (dot >= 0 && dot + 1 < g.input_name.len)
    {
        str8 ext = str8_suffix(g.input_name, g.input_name.len - dot - 1);
        c = mel_compress_for_ext(ext);
        if (c)
            return c;
    }
    return NULL;
}

static bool input_is_zip(void) { return g.input.len >= 4 && g.input.data[0] == 'P' && g.input.data[1] == 'K' && g.input.data[2] == 3 && g.input.data[3] == 4; }

static void extract_zip(void)
{
    Mel_Compress_Status st = MEL_COMPRESS_OK;
    Mel_Zip_Reader*     r = mel_zip_open(g.input, g.alloc, &st);
    if (!r)
    {
        set_status("zip archive is unreadable");
        return;
    }
    usize count = mel_zip_count(r);
    usize biggest = count;
    for (usize i = 0; i < count && i < g.codec_count; i++)
    {
        Mel_Zip_Entry e = mel_zip_entry(r, i);
        char          nbuf[64], buf[320];
        fmt_bytes(nbuf, sizeof nbuf, e.size);
        snprintf(buf, sizeof buf, "%s%.*s — %s", e.dir ? "[dir] " : "", (int)e.name.len, e.name.data, nbuf);
        set_label(g.result_labels[i], buf);
    }
    for (usize i = 0; i < count; i++)
    {
        Mel_Zip_Entry e = mel_zip_entry(r, i);
        if (e.dir)
            continue;
        if (biggest == count || e.size > mel_zip_entry(r, biggest).size)
            biggest = i;
    }
    char buf[256];
    if (biggest == count)
    {
        snprintf(buf, sizeof buf, "zip archive: %u entries, none extractable", (u32)count);
        set_status(buf);
        mel_zip_close(r);
        return;
    }

    Mel_Zip_Entry       e = mel_zip_entry(r, biggest);
    Mel_Compress_Result data = mel_zip_extract(r, biggest);
    if (mel_compress_status_failed(data.status))
    {
        set_status("zip extraction failed (corrupt entry)");
        mel_zip_close(r);
        return;
    }
    drop_output();
    g.output = data.data;
    g.output_len = data.len;
    g.have_output = true;
    g.output_decompressed = true;
    g.output_codec_id = S8("zip");

    char nbuf[64];
    fmt_bytes(nbuf, sizeof nbuf, data.len);
    snprintf(buf, sizeof buf, "zip: %u entries — extracted largest \"%.*s\" (%s)", (u32)count, (int)e.name.len, e.name.data, nbuf);
    set_status(buf);
    set_label(g.output_label, buf);
    mel_zip_close(r);
    controls_set_busy(false);
    mel_gui_set_enabled(g.save_btn, true);
}

static void start_job(bool decompress)
{
    if (!g.have_input || g.busy)
        return;

    const Mel_Compress_Codec* codec;
    if (decompress)
    {
        if (input_is_zip())
        {
            extract_zip();
            return;
        }
        codec = resolve_decompress_codec();
        if (!codec)
        {
            set_status("unknown format — no codec magic matched and the extension is unmapped");
            return;
        }
    }
    else
        codec = mel_compress_at(g.selected);

    drop_output();
    controls_set_busy(true);

    g.job = (Lab_Job){
        .codec = codec,
        .alloc = g.alloc,
        .decompress = decompress,
        .level = decompress ? 0 : level_from_slider(codec),
        .in = g.input,
    };
    if (!lab_job_open(&g.job))
    {
        char buf[128];
        snprintf(buf, sizeof buf, "could not start — status 0x%x", g.job.status);
        set_status(buf);
        controls_set_busy(false);
        return;
    }

    g.pump_frame = (Mel_Coro_Frame_lab_pump){ 0 };
    g.pump_frame.job = &g.job;
    g.job_live = true;
    mel_slider_set_value(g.progress_bar, 0);
    start_tick();
}

static void on_compress_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    start_job(false);
}

static void on_decompress_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    start_job(true);
}

static void on_race_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (!g.have_input || g.busy)
        return;
    drop_output();
    controls_set_busy(true);

    memset(g.race.results, 0, sizeof(Lab_Race_Result) * g.codec_count);
    g.race.alloc = g.alloc;
    g.race.input = g.input;
    g.race.count = g.codec_count;
    g.race.current = 0;

    g.race_frame = (Mel_Coro_Frame_lab_race_run){ 0 };
    g.race_frame.race = &g.race;
    g.race_live = true;
    mel_slider_set_value(g.progress_bar, 0);
    update_race_labels();
    set_label(g.summary_label, "");
    start_tick();
}

static void make_sample(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g.busy)
        return;
    u8* data = mel_alloc(g.alloc, LAB_SAMPLE_LEN);
    if (!data)
        return;
    static const char para[] = "Melody plays on every platform she meets; the engine hides her complexity, never her power. ";
    usize             pos = 0;
    u32               line = 0;
    while (pos < LAB_SAMPLE_LEN)
    {
        char  buf[160];
        int   n = snprintf(buf, sizeof buf, "%06u %s\n", line++, para);
        usize take = (usize)n < LAB_SAMPLE_LEN - pos ? (usize)n : LAB_SAMPLE_LEN - pos;
        memcpy(data + pos, buf, take);
        pos += take;
    }
    set_input(data, LAB_SAMPLE_LEN, S8("sample.txt"));
    set_status("sample data generated");
}

static char* dup_cstr(const char* s)
{
    usize n = strlen(s);
    char* p = mel_alloc(g.alloc, n + 1);
    if (p)
        memcpy(p, s, n + 1);
    return p;
}

static str8 path_basename(const char* path)
{
    str8 p = str8_from_cstr(path);
    size slash = str8_rfind(p, S8("/"));
    if (slash < 0)
        slash = str8_rfind(p, S8("\\"));
    if (slash >= 0)
        return str8_suffix(p, p.len - slash - 1);
    return p;
}

static void on_load_done(Mel_Task* t)
{
    (void)t;
    Mel_Future* f = g.pending;
    g.pending = NULL;
    const Mel_IO_Blob* blob = mel_io_load_future_result(f);
    if (!blob || mel_io_status_failed(blob->status))
    {
        set_status("failed to read the file");
        mel_io_load_future_release(f);
        controls_set_busy(false);
        return;
    }
    u8* copy = mel_alloc(g.alloc, blob->len ? blob->len : 1);
    if (copy && blob->len)
        memcpy(copy, blob->data, blob->len);
    usize len = blob->len;
    mel_io_load_future_release(f);
    if (!copy)
    {
        set_status("out of memory while loading");
        controls_set_busy(false);
        return;
    }
    set_input(copy, len, path_basename(g.input_path));
    set_status("file loaded");
    controls_set_busy(false);
}

static void on_open_picked(Mel_Task* t)
{
    (void)t;
    Mel_Future* f = g.pending;
    g.pending = NULL;
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    if (!sel || mel_dialog_status_failed(sel->status) || mel_dialog_status_cancelled(sel->status) || sel->path_count == 0)
    {
        mel_dialog_future_free(f);
        controls_set_busy(false);
        return;
    }
    if (g.input_path)
        mel_dealloc(g.alloc, g.input_path);
    g.input_path = dup_cstr(sel->paths[0]);
    mel_dialog_future_free(f);

    g.pending = mel_io_load_file(.path = g.input_path, .vat = g.vat, .alloc = g.alloc);
    if (!g.pending)
    {
        set_status("failed to start reading the file");
        controls_set_busy(false);
        return;
    }
    mel_task_init(&g.stage_task, on_load_done);
    mel_future_then(g.pending, &g.stage_task, g.exec);
}

static void on_open_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g.busy)
        return;
    if (!mel_dialog_available())
    {
        set_status("no file dialog on this platform — use sample data");
        return;
    }
    controls_set_busy(true);
    g.pending = mel_dialog_open_file(.title = "Choose a file", .vat = g.vat, .alloc = g.alloc);
    if (!g.pending)
    {
        set_status("could not open the file dialog");
        controls_set_busy(false);
        return;
    }
    mel_task_init(&g.stage_task, on_open_picked);
    mel_future_then(g.pending, &g.stage_task, g.exec);
}

static void suggested_save_name(char* buf, usize cap)
{
    str8 name = g.input_name;
    if (g.output_decompressed)
    {
        size dot = str8_rfind(name, S8("."));
        if (dot > 0)
            snprintf(buf, cap, "%.*s", (int)dot, name.data);
        else
            snprintf(buf, cap, "%.*s.out", (int)name.len, name.data);
        return;
    }
    const Mel_Compress_Codec* c = mel_compress_at(g.selected);
    snprintf(buf, cap, "%.*s.%.*s", (int)name.len, name.data, (int)c->ext.len, c->ext.data);
}

static void on_save_done(Mel_Task* t)
{
    (void)t;
    Mel_Future* f = g.pending;
    g.pending = NULL;
    const Mel_IO_Result* r = mel_io_save_future_result(f);
    set_status(r && !mel_io_status_failed(r->status) ? "output saved" : "saving failed");
    mel_io_save_future_release(f);
    controls_set_busy(false);
}

static void on_save_picked(Mel_Task* t)
{
    (void)t;
    Mel_Future* f = g.pending;
    g.pending = NULL;
    const Mel_Dialog_Selection* sel = mel_dialog_future_selection(f);
    if (!sel || mel_dialog_status_cancelled(sel->status) || sel->path_count == 0)
    {
        mel_dialog_future_free(f);
        controls_set_busy(false);
        return;
    }
    if (g.save_path)
        mel_dealloc(g.alloc, g.save_path);
    g.save_path = dup_cstr(sel->paths[0]);
    mel_dialog_future_free(f);

    g.pending = mel_io_save_file(.path = g.save_path, .data = g.output, .len = g.output_len, .vat = g.vat, .alloc = g.alloc);
    if (!g.pending)
    {
        set_status("failed to start saving");
        controls_set_busy(false);
        return;
    }
    mel_task_init(&g.stage_task, on_save_done);
    mel_future_then(g.pending, &g.stage_task, g.exec);
}

static void on_save_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g.busy || !g.have_output)
        return;
    if (!mel_dialog_available())
    {
        set_status("no save dialog on this platform");
        return;
    }
    char name[320];
    suggested_save_name(name, sizeof name);
    controls_set_busy(true);
    g.pending = mel_dialog_save_file(.title = "Save output", .default_name = name, .vat = g.vat, .alloc = g.alloc);
    if (!g.pending)
    {
        set_status("could not open the save dialog");
        controls_set_busy(false);
        return;
    }
    mel_task_init(&g.stage_task, on_save_picked);
    mel_future_then(g.pending, &g.stage_task, g.exec);
}

static void build_screen(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Compress Lab"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Pick an input, pick a codec, and pump it through the streaming compressor — chunk by chunk, on a coro continuation, without ever blocking this window."), .layoutable = { .preferred_h = 40 });

    Mel_Gui_Handle file_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 34 });
    g.open_btn = mel_button_create(file_row, .text = S8("Open file…"), .pointer.on_click = on_open_clicked, .layoutable = { .preferred_w = 120 });
    g.sample_btn = mel_button_create(file_row, .text = S8("Sample data"), .pointer.on_click = make_sample, .layoutable = { .preferred_w = 120 });
    g.file_label = mel_label_create(file_row, .layoutable = { .weight = 1 });

    Mel_Gui_Handle codec_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 6), .layoutable = { .preferred_h = 34 });
    for (usize i = 0; i < g.codec_count; i++)
        g.codec_btns[i] = mel_button_create(codec_row, .pointer.on_click = on_codec_clicked, .user = (void*)i, .layoutable = { .weight = 1 });

    Mel_Gui_Handle level_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 32 });
    g.level_slider = mel_slider_create(level_row, .min_value = 0, .max_value = 100, .on_.on_value_changed = on_level_changed, .layoutable = { .weight = 1 });
    g.level_label = mel_label_create(level_row, .layoutable = { .preferred_w = 320 });

    Mel_Gui_Handle action_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 38 });
    g.compress_btn = mel_button_create(action_row, .text = S8("Compress"), .pointer.on_click = on_compress_clicked, .layoutable = { .weight = 1 });
    g.decompress_btn = mel_button_create(action_row, .text = S8("Decompress (auto-detect)"), .pointer.on_click = on_decompress_clicked, .layoutable = { .weight = 1 });
    g.race_btn = mel_button_create(action_row, .text = S8("Race every codec"), .pointer.on_click = on_race_clicked, .layoutable = { .weight = 1 });

    g.progress_bar = mel_slider_create(frame, .min_value = 0, .max_value = LAB_PROGRESS_MAX, .disabled = true, .layoutable = { .preferred_h = 18 });
    g.status_label = mel_label_create(frame, .text = S8("ready"), .layoutable = { .preferred_h = 24 });

    for (usize i = 0; i < g.codec_count; i++)
        g.result_labels[i] = mel_label_create(frame, .layoutable = { .preferred_h = 22 });
    g.summary_label = mel_label_create(frame, .layoutable = { .preferred_h = 24 });

    Mel_Gui_Handle out_row = mel_panel_create(frame, .layout = mel_row_layout(.spacing = 8), .layoutable = { .preferred_h = 34 });
    g.output_label = mel_label_create(out_row, .text = S8("no output yet"), .layoutable = { .weight = 1 });
    g.save_btn = mel_button_create(out_row, .text = S8("Save output…"), .pointer.on_click = on_save_clicked, .disabled = true, .layoutable = { .preferred_w = 140 });

    update_file_label();
    select_codec(g.selected);
}

void lab_ui_setup(Mel_Vat* root)
{
    memset(&g, 0, sizeof g);
    g.vat = root;
    g.exec = mel_vat_executor(root);
    g.alloc = mel_alloc_heap();

    g.codec_count = mel_compress_count();
    g.codec_btns = mel_alloc_array(g.alloc, Mel_Gui_Handle, g.codec_count);
    g.result_labels = mel_alloc_array(g.alloc, Mel_Gui_Handle, g.codec_count);
    g.race.results = mel_alloc_array(g.alloc, Lab_Race_Result, g.codec_count);

    const Mel_Compress_Codec* zstd = mel_compress_find(S8("zstd"));
    for (usize i = 0; i < g.codec_count; i++)
        if (mel_compress_at(i) == zstd)
            g.selected = i;

    g.tick = mel_vat_tick_open(root, g.alloc, LAB_TICK_NS, lab_tick, NULL);
    mel_vat_tick_pause(g.tick);

    mel_app_register_screen(S8("lab"), build_screen, NULL);
    mel_app_present(S8("lab"), NULL);
}
