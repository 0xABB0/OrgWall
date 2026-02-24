#include "async.io.h"
#include "allocator.h"
#include "collection.ring.h"
#include "collection.hashmap.h"

#include <SDL3/SDL_timer.h>
#include <string.h>

static usize mel__io_round_pow2(usize v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

static bool mel__io_all_sq_empty(Mel_Io* io)
{
    for (u32 i = 0; i < MEL_IO_QOS_LANE_COUNT; i++)
        if (!mel_ring_empty(&io->sq_lanes[i])) return false;
    return true;
}

static i32 mel__io_first_nonempty_lane(Mel_Io* io)
{
    for (u32 i = 0; i < MEL_IO_QOS_LANE_COUNT; i++)
        if (!mel_ring_empty(&io->sq_lanes[i])) return (i32)i;
    return -1;
}

static void mel__io_post_cqe(Mel_Io* io, Mel_Io_Cqe cqe)
{
    SDL_LockMutex(io->cq_lock);

    while (mel_ring_full(&io->cq) && !SDL_GetAtomicInt(&io->shutdown_flag))
    {
        SDL_WaitCondition(io->cq_cond, io->cq_lock);
    }

    if (!mel_ring_full(&io->cq))
    {
        mel_ring_push(&io->cq, cqe);
        SDL_BroadcastCondition(io->cq_cond);
    }

    SDL_UnlockMutex(io->cq_lock);
}

static void mel__io_execute_sqe(Mel_Io* io, const Mel_Io_Sqe* sqe)
{
    Mel_Io_Cqe cqe = {
        .ticket = sqe->ticket,
        .handler_id = sqe->handler_id,
        .status = MEL_IO_STATUS_OK,
        .user_data = sqe->user_data,
        .result_data = sqe->op_data,
    };

    if (sqe->handler_id < io->handler_count && io->handlers[sqe->handler_id].fn)
    {
        io->handlers[sqe->handler_id].fn(io->handlers[sqe->handler_id].ctx, sqe, &cqe);
    }
    else
    {
        cqe.status = MEL_IO_STATUS_ERROR;
    }

    mel__io_post_cqe(io, cqe);
}

static bool mel__io_check_cancel(Mel_Io* io, u64 ticket)
{
    void* key = (void*)(usize)ticket;
    if (mel_hashmap_contains(&io->cancel_set, key))
    {
        mel_hashmap_remove(&io->cancel_set, key);
        return true;
    }
    return false;
}

static bool mel__io_check_deadline(const Mel_Io_Sqe* sqe)
{
    if (sqe->deadline_ns == 0) return false;
    return SDL_GetTicksNS() > sqe->deadline_ns;
}

static void mel__io_drain_sync(Mel_Io* io)
{
    bool link_failed = false;

    for (;;)
    {
        SDL_LockMutex(io->sq_lock);
        i32 lane = mel__io_first_nonempty_lane(io);
        if (lane < 0)
        {
            SDL_UnlockMutex(io->sq_lock);
            break;
        }
        Mel_Io_Sqe sqe = mel_ring_pop(&io->sq_lanes[lane]);
        bool cancelled = !link_failed && mel__io_check_cancel(io, sqe.ticket);
        SDL_UnlockMutex(io->sq_lock);

        if (link_failed)
        {
            Mel_Io_Cqe cqe = {
                .ticket = sqe.ticket,
                .handler_id = sqe.handler_id,
                .status = MEL_IO_STATUS_CANCELLED,
                .user_data = sqe.user_data,
                .result_data = sqe.op_data,
            };
            mel__io_post_cqe(io, cqe);

            if (!(sqe.flags & MEL_IO_SQE_F_LINK_NEXT))
                link_failed = false;

            continue;
        }

        if (cancelled)
        {
            Mel_Io_Cqe cqe = {
                .ticket = sqe.ticket,
                .handler_id = sqe.handler_id,
                .status = MEL_IO_STATUS_CANCELLED,
                .user_data = sqe.user_data,
                .result_data = sqe.op_data,
            };
            mel__io_post_cqe(io, cqe);

            if (sqe.flags & MEL_IO_SQE_F_LINK_NEXT)
                link_failed = true;

            continue;
        }

        if (mel__io_check_deadline(&sqe))
        {
            Mel_Io_Cqe cqe = {
                .ticket = sqe.ticket,
                .handler_id = sqe.handler_id,
                .status = MEL_IO_STATUS_TIMEOUT,
                .user_data = sqe.user_data,
                .result_data = sqe.op_data,
            };
            mel__io_post_cqe(io, cqe);

            if (sqe.flags & MEL_IO_SQE_F_LINK_NEXT)
                link_failed = true;

            continue;
        }

        if (sqe.flags & MEL_IO_SQE_F_FENCE)
        {
        }

        Mel_Io_Cqe cqe = {
            .ticket = sqe.ticket,
            .handler_id = sqe.handler_id,
            .status = MEL_IO_STATUS_OK,
            .user_data = sqe.user_data,
        };

        if (sqe.handler_id < io->handler_count && io->handlers[sqe.handler_id].fn)
        {
            io->handlers[sqe.handler_id].fn(io->handlers[sqe.handler_id].ctx, &sqe, &cqe);
        }
        else
        {
            cqe.status = MEL_IO_STATUS_ERROR;
        }

        mel__io_post_cqe(io, cqe);

        if ((sqe.flags & MEL_IO_SQE_F_LINK_NEXT) && cqe.status != MEL_IO_STATUS_OK)
            link_failed = true;
    }
}

static int SDLCALL mel__io_worker_fn(void* data)
{
    Mel_Io* io = (Mel_Io*)data;
    bool link_failed = false;

    for (;;)
    {
        SDL_LockMutex(io->sq_lock);

        while (mel__io_all_sq_empty(io) && !SDL_GetAtomicInt(&io->shutdown_flag))
        {
            SDL_WaitCondition(io->sq_cond, io->sq_lock);
        }

        if (SDL_GetAtomicInt(&io->shutdown_flag) && mel__io_all_sq_empty(io))
        {
            SDL_UnlockMutex(io->sq_lock);
            break;
        }

        i32 lane = mel__io_first_nonempty_lane(io);
        assert(lane >= 0);
        Mel_Io_Sqe sqe = mel_ring_pop(&io->sq_lanes[lane]);
        bool cancelled = !link_failed && mel__io_check_cancel(io, sqe.ticket);
        SDL_UnlockMutex(io->sq_lock);

        if (link_failed)
        {
            Mel_Io_Cqe cqe = {
                .ticket = sqe.ticket,
                .handler_id = sqe.handler_id,
                .status = MEL_IO_STATUS_CANCELLED,
                .user_data = sqe.user_data,
                .result_data = sqe.op_data,
            };
            mel__io_post_cqe(io, cqe);

            if (!(sqe.flags & MEL_IO_SQE_F_LINK_NEXT))
                link_failed = false;

            continue;
        }

        if (cancelled)
        {
            Mel_Io_Cqe cqe = {
                .ticket = sqe.ticket,
                .handler_id = sqe.handler_id,
                .status = MEL_IO_STATUS_CANCELLED,
                .user_data = sqe.user_data,
                .result_data = sqe.op_data,
            };
            mel__io_post_cqe(io, cqe);

            if (sqe.flags & MEL_IO_SQE_F_LINK_NEXT)
                link_failed = true;

            continue;
        }

        if (mel__io_check_deadline(&sqe))
        {
            Mel_Io_Cqe cqe = {
                .ticket = sqe.ticket,
                .handler_id = sqe.handler_id,
                .status = MEL_IO_STATUS_TIMEOUT,
                .user_data = sqe.user_data,
                .result_data = sqe.op_data,
            };
            mel__io_post_cqe(io, cqe);

            if (sqe.flags & MEL_IO_SQE_F_LINK_NEXT)
                link_failed = true;

            continue;
        }

        mel__io_execute_sqe(io, &sqe);

        if ((sqe.flags & MEL_IO_SQE_F_LINK_NEXT))
        {
            SDL_LockMutex(io->cq_lock);
            Mel_Io_Cqe last = mel_ring_peek_back(&io->cq);
            bool failed = last.ticket == sqe.ticket && last.status != MEL_IO_STATUS_OK;
            SDL_UnlockMutex(io->cq_lock);

            if (failed)
                link_failed = true;
        }
    }

    return 0;
}

bool mel_io_init(Mel_Io* io, const Mel_Io_Desc* desc)
{
    assert(io);
    assert(desc);
    assert(desc->allocator);

    memset(io, 0, sizeof(*io));
    io->alloc = desc->allocator;

    usize sq_cap = desc->sq_capacity ? mel__io_round_pow2(desc->sq_capacity) : MEL_IO_DEFAULT_SQ_CAPACITY;
    usize cq_cap = desc->cq_capacity ? mel__io_round_pow2(desc->cq_capacity) : MEL_IO_DEFAULT_CQ_CAPACITY;

    for (u32 i = 0; i < MEL_IO_QOS_LANE_COUNT; i++)
        mel_ring_init(&io->sq_lanes[i], sq_cap, io->alloc);
    mel_ring_init(&io->cq, cq_cap, io->alloc);
    mel_hashmap_init(&io->cancel_set, mel_hashmap_hash_u64, mel_hashmap_eq_u64, io->alloc);

    io->handler_count = 0;
    atomic_store(&io->ticket_counter, 1);

    io->sq_lock = SDL_CreateMutex();
    io->sq_cond = SDL_CreateCondition();
    io->cq_lock = SDL_CreateMutex();
    io->cq_cond = SDL_CreateCondition();
    io->drain_lock = SDL_CreateMutex();

    SDL_SetAtomicInt(&io->shutdown_flag, 0);
    io->worker_count = (desc->worker_count > 0) ? 1u : 0u;

    if (io->worker_count > 0)
    {
        io->workers = mel_alloc(io->alloc, sizeof(SDL_Thread*) * io->worker_count);
        for (u32 i = 0; i < io->worker_count; i++)
        {
            io->workers[i] = SDL_CreateThread(mel__io_worker_fn, "io_worker", io);
        }
    }

    return true;
}

void mel_io_shutdown(Mel_Io* io)
{
    assert(io);

    SDL_SetAtomicInt(&io->shutdown_flag, 1);

    SDL_LockMutex(io->sq_lock);
    SDL_BroadcastCondition(io->sq_cond);
    SDL_UnlockMutex(io->sq_lock);
    SDL_LockMutex(io->cq_lock);
    SDL_BroadcastCondition(io->cq_cond);
    SDL_UnlockMutex(io->cq_lock);

    if (io->workers)
    {
        for (u32 i = 0; i < io->worker_count; i++)
        {
            SDL_WaitThread(io->workers[i], NULL);
        }
        mel_dealloc(io->alloc, io->workers);
        io->workers = NULL;
    }

    for (u32 i = 0; i < MEL_IO_QOS_LANE_COUNT; i++)
        mel_ring_free(&io->sq_lanes[i]);
    mel_ring_free(&io->cq);
    mel_hashmap_free(&io->cancel_set);

    SDL_DestroyCondition(io->cq_cond);
    SDL_DestroyMutex(io->cq_lock);
    SDL_DestroyCondition(io->sq_cond);
    SDL_DestroyMutex(io->sq_lock);
    SDL_DestroyMutex(io->drain_lock);
}

u16 mel_io_register_handler(Mel_Io* io, Mel_Io_Execute_Fn fn, void* ctx)
{
    assert(io);
    assert(fn);
    assert(io->handler_count < MEL_IO_MAX_HANDLERS);

    u16 id = io->handler_count++;
    io->handlers[id] = (Mel_Io__Handler){ .fn = fn, .ctx = ctx };
    return id;
}

u64 mel_io_next_ticket(Mel_Io* io)
{
    return atomic_fetch_add(&io->ticket_counter, 1);
}

bool mel_io_cancel(Mel_Io* io, u64 target_ticket)
{
    assert(io);
    assert(target_ticket != 0);

    SDL_LockMutex(io->sq_lock);
    mel_hashmap_put(&io->cancel_set, (void*)(usize)target_ticket, (void*)(usize)1);
    SDL_UnlockMutex(io->sq_lock);

    return true;
}

i32 mel_io_submit(Mel_Io* io, const Mel_Io_Sqe* sqes, i32 count)
{
    assert(io);
    assert(sqes || count == 0);

    SDL_LockMutex(io->sq_lock);

    i32 accepted = 0;

    while (accepted < count)
    {
        i32 chain_end = accepted;
        while (chain_end < count && (sqes[chain_end].flags & MEL_IO_SQE_F_LINK_NEXT))
            chain_end++;
        i32 chain_len = chain_end - accepted + 1;

        u8 lane_idx = sqes[accepted].qos_class;
        if (lane_idx >= MEL_IO_QOS_LANE_COUNT)
            lane_idx = MEL_IO_QOS_LANE_COUNT - 1;

        usize available = io->sq_lanes[lane_idx].capacity - io->sq_lanes[lane_idx].count;
        if ((usize)chain_len > available) break;

        for (i32 i = accepted; i <= chain_end && i < count; i++)
        {
            assert(sqes[i].ticket != 0);
            mel_ring_push(&io->sq_lanes[lane_idx], sqes[i]);
        }

        accepted = chain_end + 1;
    }

    if (accepted > 0 && io->worker_count > 0)
    {
        SDL_BroadcastCondition(io->sq_cond);
    }

    SDL_UnlockMutex(io->sq_lock);

    if (io->worker_count == 0 && accepted > 0)
    {
        SDL_LockMutex(io->drain_lock);
        mel__io_drain_sync(io);
        SDL_UnlockMutex(io->drain_lock);
    }

    return accepted;
}

i32 mel_io_poll(Mel_Io* io, Mel_Io_Cqe* out, i32 max_count)
{
    assert(io);
    assert(out || max_count == 0);

    SDL_LockMutex(io->cq_lock);

    i32 n = 0;
    while (n < max_count && !mel_ring_empty(&io->cq))
    {
        out[n++] = mel_ring_pop(&io->cq);
    }

    if (n > 0)
    {
        SDL_BroadcastCondition(io->cq_cond);
    }

    SDL_UnlockMutex(io->cq_lock);
    return n;
}

bool mel_io_wait(Mel_Io* io, i32 min_count, u32 timeout_ms)
{
    assert(io);

    SDL_LockMutex(io->cq_lock);

    while ((i32)io->cq.count < min_count)
    {
        if (!SDL_WaitConditionTimeout(io->cq_cond, io->cq_lock, (i32)timeout_ms))
        {
            SDL_UnlockMutex(io->cq_lock);
            return (i32)io->cq.count >= min_count;
        }
    }

    SDL_UnlockMutex(io->cq_lock);
    return true;
}

bool mel_io_poll_ticket(Mel_Io* io, u64 ticket, Mel_Io_Cqe* out)
{
    assert(io);
    assert(out);

    SDL_LockMutex(io->cq_lock);

    for (usize i = 0; i < io->cq.count; i++)
    {
        Mel_Io_Cqe cqe = mel_ring_at(&io->cq, i);
        if (cqe.ticket == ticket)
        {
            *out = cqe;

            usize actual_idx = (io->cq.head + i) % io->cq.capacity;
            usize last_idx = (io->cq.head + io->cq.count - 1) % io->cq.capacity;
            if (actual_idx != last_idx)
            {
                io->cq.items[actual_idx] = io->cq.items[last_idx];
            }
            io->cq.count--;
            SDL_BroadcastCondition(io->cq_cond);

            SDL_UnlockMutex(io->cq_lock);
            return true;
        }
    }

    SDL_UnlockMutex(io->cq_lock);
    return false;
}

bool mel_io_wait_ticket(Mel_Io* io, u64 ticket, u32 timeout_ms, Mel_Io_Cqe* out)
{
    assert(io);
    assert(out);

    SDL_LockMutex(io->cq_lock);

    for (;;)
    {
        for (usize i = 0; i < io->cq.count; i++)
        {
            Mel_Io_Cqe cqe = mel_ring_at(&io->cq, i);
            if (cqe.ticket == ticket)
            {
                *out = cqe;

                usize actual_idx = (io->cq.head + i) % io->cq.capacity;
                usize last_idx = (io->cq.head + io->cq.count - 1) % io->cq.capacity;
                if (actual_idx != last_idx)
                {
                    io->cq.items[actual_idx] = io->cq.items[last_idx];
                }
                io->cq.count--;
                SDL_BroadcastCondition(io->cq_cond);

                SDL_UnlockMutex(io->cq_lock);
                return true;
            }
        }

        if (!SDL_WaitConditionTimeout(io->cq_cond, io->cq_lock, (i32)timeout_ms))
        {
            SDL_UnlockMutex(io->cq_lock);
            return false;
        }
    }
}
