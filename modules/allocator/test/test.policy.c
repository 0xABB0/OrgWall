#include <allocator.heap/allocator.heap.h>
#include "test.harness.h"

MEL_TEST(allocator_heap_reports_compile_time_memory_debug_level, .tags = "allocator")
{
    MEL_ASSERT_EQ(mel_alloc_heap_memory_debug_level(), (u32)MEL_MEMORY_DEBUG);
}
