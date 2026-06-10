#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat      Mel_Vat;
typedef struct Mel_Executor Mel_Executor;

typedef struct Mel_Port Mel_Port;

typedef u32 Mel_Port_Status;

#define MEL_PORT_SEVERITY_MASK 0x3u
#define MEL_PORT_OK            0u
#define MEL_PORT_WARNED        1u
#define MEL_PORT_ERROR         2u

#define MEL_PORT_CANCELLED     (1u << 2)
#define MEL_PORT_EOF           (1u << 3)
#define MEL_PORT_PEER_CLOSE    (1u << 4)
#define MEL_PORT_PARTIAL       (1u << 5)
#define MEL_PORT_BAD_FD        (1u << 6)
#define MEL_PORT_UNAVAILABLE   (1u << 7)

static inline bool mel_port_status_failed(Mel_Port_Status s) { return (s & MEL_PORT_SEVERITY_MASK) == MEL_PORT_ERROR; }
static inline bool mel_port_status_warned(Mel_Port_Status s) { return (s & MEL_PORT_SEVERITY_MASK) == MEL_PORT_WARNED; }
static inline bool mel_port_status_cancelled(Mel_Port_Status s) { return (s & MEL_PORT_CANCELLED) != 0u; }
static inline bool mel_port_status_eof(Mel_Port_Status s) { return (s & MEL_PORT_EOF) != 0u; }

typedef struct
{
    usize           bytes_transferred;
    i32             os_error;
    Mel_Port_Status status;
} Mel_Port_Result;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Port_Op;

#define MEL_PORT_OP_NULL ((Mel_Port_Op){ 0, 0 })

static inline bool mel_port_op_valid(Mel_Port_Op op) { return op.index != 0 || op.generation != 0; }

typedef struct
{
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
} Mel_Port_Opt;

Mel_Port* mel_port_create_opt(Mel_Port_Opt opt);
#define mel_port_create(...) mel_port_create_opt((Mel_Port_Opt){ __VA_ARGS__ })

void mel_port_destroy(Mel_Port* port);

bool          mel_port_available(const Mel_Port* port);
Mel_Vat*      mel_port_vat(const Mel_Port* port);
Mel_Executor* mel_port_executor(const Mel_Port* port);

typedef struct
{
    i32           fd;
    void*         buffer;
    usize         len;
    i64           offset;
    Mel_Executor* deliver;
    Mel_Port_Op*  out_op;
} Mel_Port_Read_Opt;

typedef struct
{
    i32           fd;
    const void*   buffer;
    usize         len;
    i64           offset;
    Mel_Executor* deliver;
    Mel_Port_Op*  out_op;
} Mel_Port_Write_Opt;

#define MEL_PORT_NO_OFFSET ((i64) - 1)

Mel_Future* mel_port_read_opt(Mel_Port* port, Mel_Port_Read_Opt opt);
#define mel_port_read(port, ...) mel_port_read_opt((port), (Mel_Port_Read_Opt){ .offset = MEL_PORT_NO_OFFSET, __VA_ARGS__ })

Mel_Future* mel_port_write_opt(Mel_Port* port, Mel_Port_Write_Opt opt);
#define mel_port_write(port, ...) mel_port_write_opt((port), (Mel_Port_Write_Opt){ .offset = MEL_PORT_NO_OFFSET, __VA_ARGS__ })

bool mel_port_cancel(Mel_Port* port, Mel_Port_Op op);

u32 mel_port_pending(const Mel_Port* port);

const Mel_Port_Result* mel_port_future_result(Mel_Future* f);
void                   mel_port_future_release(Mel_Future* f);

#ifdef __cplusplus
}
#endif
