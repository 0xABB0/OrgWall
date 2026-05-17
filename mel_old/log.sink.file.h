#pragma once

#include "log.fwd.h"
#include "str8.fwd.h"

typedef struct {
    str8 file_path;
} Mel_Log_Sink_File_Opt;

Mel_Log_Sink* mel_log_sink_file_create_opt(Mel_Log_Sink_File_Opt opt);
#define mel_log_sink_file_create(...) mel_log_sink_file_create_opt((Mel_Log_Sink_File_Opt){__VA_ARGS__})
