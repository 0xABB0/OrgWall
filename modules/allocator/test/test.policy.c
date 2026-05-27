#include <allocator/guard.cfg.h>
#include <allocator/heap.h>
#include <test/test.h>

MEL_TEST(alloc_policy, heap_reports_compile_time_memory_debug_level)
{
    MEL_REQUIRE_EQ(mel_alloc_heap_memory_debug_level(), (u32)MEL_MEMORY_DEBUG);
}
