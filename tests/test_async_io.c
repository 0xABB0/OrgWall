#include "../melody/test.harness.h"
#include "../melody/async.io.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

typedef struct {
    i32 value;
} TestOpData;

typedef struct {
    i32 result_value;
} TestResultData;

static void test_handler(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe)
{
    MEL_UNUSED(ctx);
    TestOpData* op = (TestOpData*)sqe->op_data;
    cqe->result = op ? op->value * 2 : 0;
    cqe->status = MEL_IO_STATUS_OK;
}

static void test_failing_handler(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(sqe);
    cqe->status = MEL_IO_STATUS_ERROR;
    cqe->result = -1;
}

static void test_ctx_handler(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe)
{
    MEL_UNUSED(sqe);
    i32* counter = (i32*)ctx;
    (*counter)++;
    cqe->status = MEL_IO_STATUS_OK;
    cqe->result = *counter;
}

MEL_TEST(io_init_shutdown, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    MEL_ASSERT(mel_io_init(&io, &desc));
    mel_io_shutdown(&io);
}

MEL_TEST(io_register_handler, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 id = mel_io_register_handler(&io, test_handler, NULL);
    MEL_ASSERT_EQ(id, (u16)0);

    u16 id2 = mel_io_register_handler(&io, test_failing_handler, NULL);
    MEL_ASSERT_EQ(id2, (u16)1);

    mel_io_shutdown(&io);
}

MEL_TEST(io_submit_poll_sync, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, test_handler, NULL);

    TestOpData op = { .value = 21 };
    u64 ticket = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe = {
        .ticket = ticket,
        .handler_id = hid,
        .op = 1,
        .op_data = &op,
    };

    i32 accepted = mel_io_submit(&io, &sqe, 1);
    MEL_ASSERT_EQ(accepted, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, ticket, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT_EQ(cqe.result, (i64)42);

    mel_io_shutdown(&io);
}

MEL_TEST(io_ticket_monotonic, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u64 t1 = mel_io_next_ticket(&io);
    u64 t2 = mel_io_next_ticket(&io);
    u64 t3 = mel_io_next_ticket(&io);

    MEL_ASSERT_GT(t1, (u64)0);
    MEL_ASSERT_GT(t2, t1);
    MEL_ASSERT_GT(t3, t2);

    mel_io_shutdown(&io);
}

MEL_TEST(io_multi_handler, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 ok_id = mel_io_register_handler(&io, test_handler, NULL);
    u16 fail_id = mel_io_register_handler(&io, test_failing_handler, NULL);

    TestOpData op = { .value = 5 };

    u64 t1 = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe1 = { .ticket = t1, .handler_id = ok_id, .op_data = &op };
    mel_io_submit(&io, &sqe1, 1);

    u64 t2 = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe2 = { .ticket = t2, .handler_id = fail_id };
    mel_io_submit(&io, &sqe2, 1);

    Mel_Io_Cqe cqe1, cqe2;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe1));
    MEL_ASSERT_EQ(cqe1.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT_EQ(cqe1.result, (i64)10);

    MEL_ASSERT(mel_io_poll_ticket(&io, t2, &cqe2));
    MEL_ASSERT_EQ(cqe2.status, (u32)MEL_IO_STATUS_ERROR);

    mel_io_shutdown(&io);
}

MEL_TEST(io_handler_context, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    i32 counter = 0;
    u16 hid = mel_io_register_handler(&io, test_ctx_handler, &counter);

    for (i32 i = 0; i < 5; i++)
    {
        u64 t = mel_io_next_ticket(&io);
        Mel_Io_Sqe sqe = { .ticket = t, .handler_id = hid };
        mel_io_submit(&io, &sqe, 1);
    }

    MEL_ASSERT_EQ(counter, 5);

    Mel_Io_Cqe cqes[8];
    i32 n = mel_io_poll(&io, cqes, 8);
    MEL_ASSERT_EQ(n, 5);

    mel_io_shutdown(&io);
}

MEL_TEST(io_link_next_cancel, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 fail_id = mel_io_register_handler(&io, test_failing_handler, NULL);
    u16 ok_id = mel_io_register_handler(&io, test_handler, NULL);

    TestOpData op = { .value = 1 };
    u64 t1 = mel_io_next_ticket(&io);
    u64 t2 = mel_io_next_ticket(&io);

    Mel_Io_Sqe chain[2] = {
        { .ticket = t1, .handler_id = fail_id, .flags = MEL_IO_SQE_F_LINK_NEXT },
        { .ticket = t2, .handler_id = ok_id, .op_data = &op },
    };

    mel_io_submit(&io, chain, 2);

    Mel_Io_Cqe cqe1, cqe2;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe1));
    MEL_ASSERT_EQ(cqe1.status, (u32)MEL_IO_STATUS_ERROR);

    MEL_ASSERT(mel_io_poll_ticket(&io, t2, &cqe2));
    MEL_ASSERT_EQ(cqe2.status, (u32)MEL_IO_STATUS_CANCELLED);

    mel_io_shutdown(&io);
}

MEL_TEST(io_link_next_success, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, test_handler, NULL);

    TestOpData op1 = { .value = 3 };
    TestOpData op2 = { .value = 7 };
    u64 t1 = mel_io_next_ticket(&io);
    u64 t2 = mel_io_next_ticket(&io);

    Mel_Io_Sqe chain[2] = {
        { .ticket = t1, .handler_id = hid, .flags = MEL_IO_SQE_F_LINK_NEXT, .op_data = &op1 },
        { .ticket = t2, .handler_id = hid, .op_data = &op2 },
    };

    mel_io_submit(&io, chain, 2);

    Mel_Io_Cqe cqe1, cqe2;
    MEL_ASSERT(mel_io_poll_ticket(&io, t1, &cqe1));
    MEL_ASSERT_EQ(cqe1.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT_EQ(cqe1.result, (i64)6);

    MEL_ASSERT(mel_io_poll_ticket(&io, t2, &cqe2));
    MEL_ASSERT_EQ(cqe2.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT_EQ(cqe2.result, (i64)14);

    mel_io_shutdown(&io);
}

MEL_TEST(io_poll_nonexistent, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(!mel_io_poll_ticket(&io, 99999, &cqe));

    mel_io_shutdown(&io);
}

MEL_TEST(io_user_data_passthrough, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, test_handler, NULL);

    i32 sentinel = 42;
    TestOpData op = { .value = 1 };
    u64 t = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe = { .ticket = t, .handler_id = hid, .op_data = &op, .user_data = &sentinel };
    mel_io_submit(&io, &sqe, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t, &cqe));
    MEL_ASSERT_EQ(cqe.user_data, (void*)&sentinel);

    mel_io_shutdown(&io);
}

MEL_TEST(io_threaded_submit_poll, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 2 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, test_handler, NULL);

    TestOpData op = { .value = 50 };
    u64 t = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe = { .ticket = t, .handler_id = hid, .op_data = &op };
    mel_io_submit(&io, &sqe, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_wait_ticket(&io, t, 5000, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_OK);
    MEL_ASSERT_EQ(cqe.result, (i64)100);

    mel_io_shutdown(&io);
}

MEL_TEST(io_chain_atomic_reject, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0, .sq_capacity = 4 };
    mel_io_init(&io, &desc);

    u16 hid = mel_io_register_handler(&io, test_handler, NULL);

    TestOpData ops[8];
    Mel_Io_Sqe sqes[8];
    for (i32 i = 0; i < 8; i++)
    {
        ops[i] = (TestOpData){ .value = i };
        sqes[i] = (Mel_Io_Sqe){
            .ticket = mel_io_next_ticket(&io),
            .handler_id = hid,
            .flags = (i < 7) ? MEL_IO_SQE_F_LINK_NEXT : 0,
            .op_data = &ops[i],
        };
    }

    i32 accepted = mel_io_submit(&io, sqes, 8);
    MEL_ASSERT_EQ(accepted, 0);

    mel_io_shutdown(&io);
}

MEL_TEST(io_invalid_handler, .tags = "async")
{
    Mel_Io io;
    Mel_Io_Desc desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&io, &desc);

    u64 t = mel_io_next_ticket(&io);
    Mel_Io_Sqe sqe = { .ticket = t, .handler_id = 255 };
    mel_io_submit(&io, &sqe, 1);

    Mel_Io_Cqe cqe;
    MEL_ASSERT(mel_io_poll_ticket(&io, t, &cqe));
    MEL_ASSERT_EQ(cqe.status, (u32)MEL_IO_STATUS_ERROR);

    mel_io_shutdown(&io);
}
