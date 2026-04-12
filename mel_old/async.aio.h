#pragma once

#include "core.types.h"
#include "async.aio.cfg.h"
#include "async.signal.fwd.h"

typedef struct Mel_Aio_Op {
    i32          fd;
    void*        buf;
    i64          size;
    i64          offset;
    Mel_Counter* counter;
    i64*         result;
    i32*         error;
} Mel_Aio_Op;

void mel_aio_init(void);
void mel_aio_shutdown(void);
void mel_aio_submit(Mel_Aio_Op* op);
i32  mel_aio_drain(void);
