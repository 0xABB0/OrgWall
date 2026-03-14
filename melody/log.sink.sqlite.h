#pragma once

#include "log.fwd.h"
#include "string.str8.fwd.h"

typedef struct {
    str8    db_path;
    u32     batch_size;
    u32     flush_interval_ms;
} Mel_Log_Sink_Sqlite_Opt;

Mel_Log_Sink* mel_log_sink_sqlite_create_opt(Mel_Log_Sink_Sqlite_Opt opt);
#define mel_log_sink_sqlite_create(...) mel_log_sink_sqlite_create_opt((Mel_Log_Sink_Sqlite_Opt){__VA_ARGS__})
