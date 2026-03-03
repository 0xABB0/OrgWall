#pragma once

#include "async.io.cfg.h"
#include "async.io.fwd.h"
#include "core.types.h"
#include "allocator.fwd.h"
#include "collection.ring.fwd.h"
#include "collection.hashmap.h"

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_thread.h>
#include <stdatomic.h>

#define MEL_IO_STATUS_OK               0
#define MEL_IO_STATUS_ERROR            1
#define MEL_IO_STATUS_CANCELLED        2
#define MEL_IO_STATUS_TIMEOUT          3
#define MEL_IO_STATUS_PENDING          4

#define MEL_IO_SQE_F_FENCE      (1u << 0)
#define MEL_IO_SQE_F_LINK_NEXT  (1u << 1)

// Execute function. If status is set to MEL_IO_STATUS_PENDING, the handler
// takes ownership of completion and MUST call mel_io_post_cqe later.
typedef void (*Mel_Io_Execute_Fn)(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe);

struct Mel_Io_Sqe {
    u64   ticket;
    u16   handler_id;
    u16   op;
    u32   flags;
    u8    priority;
    u8    qos_class;
    u64   deadline_ns;
    void* user_data;
    void* op_data;
};

struct Mel_Io_Cqe {
    u64   ticket;
    u16   handler_id;
    u32   status;
    i64   result;
    void* user_data;
    void* result_data;
};

typedef struct {
    Mel_Io_Execute_Fn fn;
    void*             ctx;
} Mel_Io__Handler;

struct Mel_Io {
    const Mel_Alloc* alloc;

    Mel_Ring(Mel_Io_Sqe) sq_lanes[MEL_IO_QOS_LANE_COUNT];
    Mel_Ring(Mel_Io_Cqe) cq;

    Mel_HashMap cancel_set;

    Mel_Io__Handler handlers[MEL_IO_MAX_HANDLERS];
    u16 handler_count;

    _Atomic u64 ticket_counter;

    SDL_Mutex*     sq_lock;
    SDL_Condition* sq_cond;
    SDL_Mutex*     cq_lock;
    SDL_Condition* cq_cond;
    SDL_Mutex*     drain_lock;

    SDL_Thread** workers;
    u32          worker_count;
    SDL_AtomicInt shutdown_flag;
};

struct Mel_Io_Desc {
    const Mel_Alloc* allocator;
    usize            sq_capacity;
    usize            cq_capacity;
    u32              worker_count;
};

bool  mel_io_init(Mel_Io* io, const Mel_Io_Desc* desc);
void  mel_io_shutdown(Mel_Io* io);
u16   mel_io_register_handler(Mel_Io* io, Mel_Io_Execute_Fn fn, void* ctx);
u64   mel_io_next_ticket(Mel_Io* io);
i32   mel_io_submit(Mel_Io* io, const Mel_Io_Sqe* sqes, i32 count);
bool  mel_io_cancel(Mel_Io* io, u64 target_ticket);
i32   mel_io_poll(Mel_Io* io, Mel_Io_Cqe* out, i32 max_count);
bool  mel_io_wait(Mel_Io* io, i32 min_count, u32 timeout_ms);
bool  mel_io_poll_ticket(Mel_Io* io, u64 ticket, Mel_Io_Cqe* out);
bool  mel_io_wait_ticket(Mel_Io* io, u64 ticket, u32 timeout_ms, Mel_Io_Cqe* out);
void  mel_io_post_cqe(Mel_Io* io, Mel_Io_Cqe cqe);
