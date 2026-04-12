#import <dispatch/dispatch.h>
#include "async.aio.h"
#include "async.signal.h"
#include "collection.mpmc.h"
#include "allocator.heap.h"
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>

static Mel_Mpmc s_completions;
static dispatch_queue_t s_dispatch_queue;
static _Atomic(bool) s_initialized;

__attribute__((constructor(250)))
void mel_aio_init(void)
{
    if (atomic_load(&s_initialized)) return;

    mel_mpmc_init(&s_completions, MEL_AIO_COMPLETION_CAPACITY, mel_alloc_heap());
    s_dispatch_queue = dispatch_queue_create("mel.aio", DISPATCH_QUEUE_CONCURRENT);

    atomic_store(&s_initialized, true);
}

__attribute__((destructor(250)))
void mel_aio_shutdown(void)
{
    if (!atomic_load(&s_initialized)) return;

    atomic_store(&s_initialized, false);

    s_dispatch_queue = nil;
    mel_mpmc_free(&s_completions);
}

void mel_aio_submit(Mel_Aio_Op* op)
{
    assert(atomic_load(&s_initialized));
    assert(op != NULL);
    assert(op->fd >= 0);
    assert(op->buf != NULL);
    assert(op->size > 0);

    dispatch_io_t channel = dispatch_io_create(
        DISPATCH_IO_RANDOM,
        op->fd,
        s_dispatch_queue,
        ^(int error) {
            (void)error;
        }
    );

    dispatch_io_set_low_water(channel, (size_t)op->size);

    Mel_Aio_Op* captured_op = op;

    dispatch_io_read(
        channel,
        (off_t)op->offset,
        (size_t)op->size,
        s_dispatch_queue,
        ^(bool done, dispatch_data_t data, int error) {
            if (!done) return;

            if (error != 0)
            {
                if (captured_op->result) *captured_op->result = -1;
                if (captured_op->error) *captured_op->error = error;
            }
            else if (data != NULL)
            {
                __block i64 total_copied = 0;
                dispatch_data_apply(data, ^bool(dispatch_data_t region, size_t region_offset, const void* buffer, size_t size) {
                    (void)region;
                    (void)region_offset;
                    i64 space = captured_op->size - total_copied;
                    i64 to_copy = (i64)size < space ? (i64)size : space;
                    memcpy((u8*)captured_op->buf + total_copied, buffer, (size_t)to_copy);
                    total_copied += to_copy;
                    return true;
                });
                if (captured_op->result) *captured_op->result = total_copied;
                if (captured_op->error) *captured_op->error = 0;
            }
            else
            {
                if (captured_op->result) *captured_op->result = 0;
                if (captured_op->error) *captured_op->error = 0;
            }

            mel_mpmc_push(&s_completions, captured_op);
            dispatch_io_close(channel, 0);
        }
    );
}

i32 mel_aio_drain(void)
{
    assert(atomic_load(&s_initialized));

    i32 count = 0;
    void* ptr = NULL;

    while (mel_mpmc_pop(&s_completions, &ptr))
    {
        Mel_Aio_Op* op = (Mel_Aio_Op*)ptr;
        if (op->counter)
            mel_counter_decrement(op->counter);
        count++;
    }

    return count;
}
