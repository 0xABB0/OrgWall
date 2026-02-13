#include "../melody/test.harness.h"
#include "../melody/async.fiber.h"

MEL_TEST(stack_init_release)
{
    Mel_Fiber_Stack fstack;
    bool ok = mel_fiber_stack_init(&fstack, 0);
    MEL_ASSERT(ok);
    MEL_ASSERT_NOT_NULL(fstack.sptr);
    MEL_ASSERT(fstack.ssize >= MEL_FIBER_DEFAULT_STACK_SIZE);
    MEL_ASSERT(fstack.ssize % 4096 == 0);
    mel_fiber_stack_release(&fstack);
    MEL_PASS();
}

MEL_TEST(stack_init_zero_default)
{
    Mel_Fiber_Stack fstack;
    bool ok = mel_fiber_stack_init(&fstack, 0);
    MEL_ASSERT(ok);
    MEL_ASSERT(fstack.ssize >= MEL_FIBER_DEFAULT_STACK_SIZE);
    mel_fiber_stack_release(&fstack);
    MEL_PASS();
}

static volatile i32 g_basic_flag = 0;

static void fiber_basic_cb(Mel_Fiber_Transfer transfer)
{
    g_basic_flag = 42;
    mel_fiber_switch(transfer.from, NULL);
}

MEL_TEST(fiber_basic_switch)
{
    g_basic_flag = 0;

    Mel_Fiber_Stack fstack;
    mel_fiber_stack_init(&fstack, 0);

    Mel_Fiber fiber = mel_fiber_create(fstack, fiber_basic_cb);
    MEL_ASSERT(fiber != MEL_FIBER_INVALID);

    mel_fiber_switch(fiber, NULL);
    MEL_ASSERT_EQ(g_basic_flag, 42);

    mel_fiber_stack_release(&fstack);
    MEL_PASS();
}

static volatile i32 g_roundtrip = 0;

static void fiber_roundtrip_cb(Mel_Fiber_Transfer transfer)
{
    g_roundtrip = 1;
    Mel_Fiber_Transfer t = mel_fiber_switch(transfer.from, NULL);

    g_roundtrip = 2;
    mel_fiber_switch(t.from, NULL);
}

MEL_TEST(fiber_roundtrip)
{
    g_roundtrip = 0;

    Mel_Fiber_Stack fstack;
    mel_fiber_stack_init(&fstack, 0);

    Mel_Fiber fiber = mel_fiber_create(fstack, fiber_roundtrip_cb);
    Mel_Fiber_Transfer t = mel_fiber_switch(fiber, NULL);
    MEL_ASSERT_EQ(g_roundtrip, 1);

    mel_fiber_switch(t.from, NULL);
    MEL_ASSERT_EQ(g_roundtrip, 2);

    mel_fiber_stack_release(&fstack);
    MEL_PASS();
}

static void* g_received_user = NULL;

static void fiber_userdata_cb(Mel_Fiber_Transfer transfer)
{
    g_received_user = transfer.user;
    mel_fiber_switch(transfer.from, (void*)0xCAFEBABE);
}

MEL_TEST(fiber_user_data)
{
    g_received_user = NULL;

    Mel_Fiber_Stack fstack;
    mel_fiber_stack_init(&fstack, 0);

    Mel_Fiber fiber = mel_fiber_create(fstack, fiber_userdata_cb);
    void* send = (void*)0xDEADBEEF;
    Mel_Fiber_Transfer t = mel_fiber_switch(fiber, send);

    MEL_ASSERT_EQ(g_received_user, send);
    MEL_ASSERT_EQ(t.user, (void*)0xCAFEBABE);

    mel_fiber_stack_release(&fstack);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Fiber Tests");

    MEL_RUN_TEST(stack_init_release);
    MEL_RUN_TEST(stack_init_zero_default);
    MEL_RUN_TEST(fiber_basic_switch);
    MEL_RUN_TEST(fiber_roundtrip);
    MEL_RUN_TEST(fiber_user_data);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
