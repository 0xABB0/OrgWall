#pragma once

#include "log.fwd.h"

typedef void (*Mel_Log_Sink_Write_Fn)(Mel_Log_Sink* self, const Mel_Log_Entry* entry);
typedef void (*Mel_Log_Sink_Flush_Fn)(Mel_Log_Sink* self);
typedef void (*Mel_Log_Sink_Destroy_Fn)(Mel_Log_Sink* self);

struct Mel_Log_Sink {
    Mel_Log_Sink_Write_Fn   write;
    Mel_Log_Sink_Flush_Fn   flush;
    Mel_Log_Sink_Destroy_Fn destroy;
    u32                     level_threshold;
};
