#include <test/test.h>

#include <jit/jit.h>

#include <allocator/heap.h>

static int   g_mock_destroyed = 0;
static void* g_mock_addr      = (void*)0x1234;

static Mel_Jit_Unit mock_add(void* self, Mel_Jit_Module* module, Mel_Jit_Result* out)
{
    (void)self;
    (void)module;
    if (out)
    {
        out->ok          = true;
        out->diagnostics = NULL;
    }
    return (Mel_Jit_Unit){ .index = 1, .generation = 1 };
}

static void* mock_lookup(void* self, const char* symbol)
{
    (void)self;
    (void)symbol;
    return g_mock_addr;
}

static void mock_destroy(void* self)
{
    (void)self;
    g_mock_destroyed = 1;
}

MEL_TEST(jit, facade_dispatches_to_backend)
{
    Mel_Jit_Backend be = { 0 };
    be.add             = mock_add;
    be.lookup          = mock_lookup;
    be.destroy         = mock_destroy;

    Mel_Jit* jit = mel_jit_create(mel_alloc_heap(), be);
    MEL_REQUIRE_NOT_NULL(jit);

    Mel_Jit_Result r = { 0 };
    Mel_Jit_Unit   u = mel_jit_add(jit, NULL, &r);
    MEL_EXPECT(r.ok);
    MEL_EXPECT_EQ(u.index, 1u);

    MEL_EXPECT_EQ(mel_jit_lookup(jit, "anything"), g_mock_addr);

    g_mock_destroyed = 0;
    mel_jit_destroy(jit);
    MEL_EXPECT_EQ(g_mock_destroyed, 1);
}

MEL_TEST(jit, create_rejects_backend_without_add)
{
    Mel_Jit_Backend empty = { 0 };
    MEL_EXPECT_NULL(mel_jit_create(mel_alloc_heap(), empty));
}
