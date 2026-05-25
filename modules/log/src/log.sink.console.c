#include <log/log.h>

#if !MEL_LOG_DISABLED

#include <log.sink/sink.h>
#include <log.sink.console/sink.console.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <stdio.h>
#include <inttypes.h>

typedef struct {
    Mel_Log_Sink base;
    bool         color;
} Mel_Log_Sink_Console;

static const char* mel__log_console_ansi(u32 level)
{
    if (level <= MEL_LOG_ERROR) return "\033[31m";
    if (level <= MEL_LOG_WARN)  return "\033[33m";
    if (level <= MEL_LOG_INFO)  return "\033[37m";
    if (level <= MEL_LOG_DEBUG) return "\033[90m";
    return "\033[2;90m";
}

static void mel__log_sink_console_write(Mel_Log_Sink* self, const Mel_Log_Entry* entry)
{
    Mel_Log_Sink_Console* console = (Mel_Log_Sink_Console*)self;

    str8 level_name = mel_log_level_name(entry->level);

    u64 secs = entry->timestamp_ns / 1000000000ULL;
    u64 ms   = (entry->timestamp_ns / 1000000ULL) % 1000ULL;

    if (console->color) {
        const char* color = mel__log_console_ansi(entry->level);
        const char* reset = "\033[0m";

        if (entry->context.len > 0) {
            fprintf(stderr,
                "%s[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] (%.*s) %.*s    (%.*s:%"PRIu32")%s\n",
                color,
                secs, ms,
                (int)level_name.len, level_name.data,
                (int)entry->domain.len, entry->domain.data,
                (int)entry->context.len, entry->context.data,
                (int)entry->message.len, entry->message.data,
                (int)entry->file.len, entry->file.data,
                entry->line,
                reset);
        } else {
            fprintf(stderr,
                "%s[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] %.*s    (%.*s:%"PRIu32")%s\n",
                color,
                secs, ms,
                (int)level_name.len, level_name.data,
                (int)entry->domain.len, entry->domain.data,
                (int)entry->message.len, entry->message.data,
                (int)entry->file.len, entry->file.data,
                entry->line,
                reset);
        }
    } else {
        if (entry->context.len > 0) {
            fprintf(stderr,
                "[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] (%.*s) %.*s    (%.*s:%"PRIu32")\n",
                secs, ms,
                (int)level_name.len, level_name.data,
                (int)entry->domain.len, entry->domain.data,
                (int)entry->context.len, entry->context.data,
                (int)entry->message.len, entry->message.data,
                (int)entry->file.len, entry->file.data,
                entry->line);
        } else {
            fprintf(stderr,
                "[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] %.*s    (%.*s:%"PRIu32")\n",
                secs, ms,
                (int)level_name.len, level_name.data,
                (int)entry->domain.len, entry->domain.data,
                (int)entry->message.len, entry->message.data,
                (int)entry->file.len, entry->file.data,
                entry->line);
        }
    }
}

static void mel__log_sink_console_flush(Mel_Log_Sink* self)
{
    (void)self;
    fflush(stderr);
}

static void mel__log_sink_console_destroy(Mel_Log_Sink* self)
{
    mel_dealloc(mel_alloc_heap(), self);
}

Mel_Log_Sink* mel_log_sink_console_create_opt(Mel_Log_Sink_Console_Opt opt)
{
    Mel_Log_Sink_Console* console = mel_alloc_type(mel_alloc_heap(), Mel_Log_Sink_Console);
    console->base = (Mel_Log_Sink){
        .write           = mel__log_sink_console_write,
        .flush           = mel__log_sink_console_flush,
        .destroy         = mel__log_sink_console_destroy,
        .level_threshold = MEL_LOG_TRACE,
    };
    console->color = opt.color;
    return &console->base;
}

#endif
