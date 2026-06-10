#pragma once

#include <port/port.h>
#include <port/backend.h>

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.h>
#include <future/future.h>
#include <vat/vat.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_Vat_Source*  source;
    Mel_Vat_Wakeable wakeable;
    void*            native;
    bool             attached;
} Mel_Port_Loop_Slot;

typedef bool (*Mel_Port__Step_Fn)(Mel_Port_Op_Record* op);

struct Mel_Port_Op_Record
{
    Mel_Port*          port;
    const Mel_Alloc*   alloc;
    Mel_SlotMap_Handle self;

    Mel_Port__Step_Fn step;
    i32               fd;
    bool              submitted;
    bool              settled;
    bool              detached;
    i64               offset;
    usize             len;
    usize             done;
    void*             buffer;

    Mel_Future      future;
    Mel_Port_Result result;
    Mel_Executor*   deliver;

    Mel_Port_Loop_Slot backend;
};

struct Mel_Port
{
    Mel_Vat*         vat;
    Mel_Executor*    executor;
    const Mel_Alloc* alloc;

    Mel_SlotMap ops;
    bool        backend_ready;
};

void mel_port__op_settle(Mel_Port_Op_Record* op, usize bytes, i32 os_error, Mel_Port_Status status);
void mel_port__op_detach(Mel_Port_Op_Record* op);

#ifdef __cplusplus
}
#endif
