#include <cpu/cpu.h>

#include <allocator/heap.h>

#include <stdio.h>

int main(void)
{
    Mel_Cpu_Info c = mel_cpu_info(mel_alloc_heap());

    printf("packages    : %u\n", c.package_count);
    printf("cores       : %u\n", c.core_count);
    printf("logical     : %u\n", c.logical_count);
    printf("L1 cache    : %u bytes\n", c.l1_cache_size);
    printf("L2 cache    : %u bytes\n", c.l2_cache_size);
    printf("L3 cache    : %u bytes\n", c.l3_cache_size);
    printf("page size   : %u bytes\n", c.page_size);
    printf("clock speed : %llu Hz\n", (unsigned long long)c.clock_speed);
    printf("cache line  : %u bytes\n", c.cache_line_size);

    int fail = 0;
    if (c.logical_count == 0)
    {
        fprintf(stderr, "FAIL: logical_count is zero\n");
        fail = 1;
    }
    if (c.page_size == 0)
    {
        fprintf(stderr, "FAIL: page_size is zero\n");
        fail = 1;
    }
    if (c.core_count != 0 && c.logical_count < c.core_count)
    {
        fprintf(stderr, "FAIL: logical_count (%u) < core_count (%u)\n", c.logical_count, c.core_count);
        fail = 1;
    }
    return fail;
}
