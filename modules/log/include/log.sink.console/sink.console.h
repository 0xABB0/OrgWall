#pragma once

#include <log/log.fwd.h>

#include <stdbool.h>

typedef struct {
    bool color;
} Mel_Log_Sink_Console_Opt;

Mel_Log_Sink* mel_log_sink_console_create_opt(Mel_Log_Sink_Console_Opt opt);
#define mel_log_sink_console_create(...) mel_log_sink_console_create_opt((Mel_Log_Sink_Console_Opt){__VA_ARGS__})
