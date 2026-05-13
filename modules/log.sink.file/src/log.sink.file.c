#include "log.h"

#if !MEL_LOG_DISABLED

#include "log.sink.h"
#include "log.sink.file.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

typedef struct {
    Mel_Log_Sink base;
    FILE*        handle;
} Mel_Log_Sink_File;

static void mel__log_sink_file_write(Mel_Log_Sink* self, const Mel_Log_Entry* entry)
{
    Mel_Log_Sink_File* file_sink = (Mel_Log_Sink_File*)self;

    str8 level_name = mel_log_level_name(entry->level);

    u64 secs = entry->timestamp_ns / 1000000000ULL;
    u64 ms   = (entry->timestamp_ns / 1000000ULL) % 1000ULL;

    if (entry->context.len > 0) {
        fprintf(file_sink->handle,
            "[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] (%.*s) %.*s    (%.*s:%"PRIu32")\n",
            secs, ms,
            (int)level_name.len, level_name.data,
            (int)entry->domain.len, entry->domain.data,
            (int)entry->context.len, entry->context.data,
            (int)entry->message.len, entry->message.data,
            (int)entry->file.len, entry->file.data,
            entry->line);
    } else {
        fprintf(file_sink->handle,
            "[%"PRIu64".%03"PRIu64"] [%.*s] [%.*s] %.*s    (%.*s:%"PRIu32")\n",
            secs, ms,
            (int)level_name.len, level_name.data,
            (int)entry->domain.len, entry->domain.data,
            (int)entry->message.len, entry->message.data,
            (int)entry->file.len, entry->file.data,
            entry->line);
    }
}

static void mel__log_sink_file_flush(Mel_Log_Sink* self)
{
    Mel_Log_Sink_File* file_sink = (Mel_Log_Sink_File*)self;
    fflush(file_sink->handle);
}

static void mel__log_sink_file_destroy(Mel_Log_Sink* self)
{
    Mel_Log_Sink_File* file_sink = (Mel_Log_Sink_File*)self;
    fclose(file_sink->handle);
    mel_dealloc(mel_alloc_heap(), file_sink);
}

Mel_Log_Sink* mel_log_sink_file_create_opt(Mel_Log_Sink_File_Opt opt)
{
    char path_buf[4096];
    assert(opt.file_path.len > 0 && opt.file_path.len < (size)sizeof(path_buf));

    memcpy(path_buf, opt.file_path.data, (usize)opt.file_path.len);
    path_buf[opt.file_path.len] = '\0';

    FILE* handle = fopen(path_buf, "a");
    assert(handle && "mel_log_sink_file_create: failed to open log file");

    Mel_Log_Sink_File* file_sink = mel_alloc_type(mel_alloc_heap(), Mel_Log_Sink_File);
    file_sink->base = (Mel_Log_Sink){
        .write           = mel__log_sink_file_write,
        .flush           = mel__log_sink_file_flush,
        .destroy         = mel__log_sink_file_destroy,
        .level_threshold = MEL_LOG_TRACE,
    };
    file_sink->handle = handle;
    return &file_sink->base;
}

#endif
