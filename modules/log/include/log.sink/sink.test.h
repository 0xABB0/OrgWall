#pragma once

#include <log/log.fwd.h>
#include <string/string.str8.fwd.h>

#include <stdbool.h>

Mel_Log_Sink* mel_log_sink_test_create(void);

void mel_log_test_clear(Mel_Log_Sink* sink);
bool mel_log_test_has_entry(Mel_Log_Sink* sink, u32 level, str8 domain);
u32  mel_log_test_count(Mel_Log_Sink* sink, u32 level);
void mel_log_test_dump(Mel_Log_Sink* sink);
