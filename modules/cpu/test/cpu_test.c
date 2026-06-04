#include <cpu/cpu.h>
#include <core/platform.h>
#include <allocator/heap.h>
#include <allocator/allocator.h>

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    Mel_Cpu_Info c = mel_cpu_info();

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

    Mel_Cpu_Caps caps = mel_cpu_caps();
    printf("features    : 0x%llx\n", (unsigned long long)caps.features);
    printf("ram total   : %llu bytes\n", (unsigned long long)caps.ram_total);
    printf("simd align  : %u bytes\n", caps.simd_align);

    if (mel_cpu_feature_mask_get() != ~(Mel_Cpu_Features)0)
    {
        fprintf(stderr, "FAIL: default feature mask is not allow-all\n");
        fail = 1;
    }
    if (mel_cpu_caps().features != caps.features)
    {
        fprintf(stderr, "FAIL: mel_cpu_caps() not pure\n");
        fail = 1;
    }
    if (mel_cpu_simd_align() != caps.simd_align)
    {
        fprintf(stderr, "FAIL: simd_align disagrees with caps snapshot\n");
        fail = 1;
    }
    if (mel_cpu_ram_total() != caps.ram_total)
    {
        fprintf(stderr, "FAIL: ram_total disagrees with caps snapshot\n");
        fail = 1;
    }

    bool any_simd = (caps.features & (MEL_CPU_FEATURE_SSE | MEL_CPU_FEATURE_SSE2 | MEL_CPU_FEATURE_SSE3 | MEL_CPU_FEATURE_SSSE3 | MEL_CPU_FEATURE_SSE41 | MEL_CPU_FEATURE_SSE42 | MEL_CPU_FEATURE_AVX | MEL_CPU_FEATURE_AVX2 | MEL_CPU_FEATURE_AVX512F | MEL_CPU_FEATURE_NEON)) != 0;
    if (any_simd && caps.simd_align != 16 && caps.simd_align != 32 && caps.simd_align != 64)
    {
        fprintf(stderr, "FAIL: simd_align (%u) not in {16,32,64} with SIMD present\n", caps.simd_align);
        fail = 1;
    }
    if (!any_simd && caps.simd_align != 0)
    {
        fprintf(stderr, "FAIL: simd_align (%u) nonzero with no SIMD\n", caps.simd_align);
        fail = 1;
    }

    if (caps.features & MEL_CPU_FEATURE_AVX512F)
    {
        if (caps.simd_align != 64)
        {
            fprintf(stderr, "FAIL: AVX-512F present but simd_align != 64\n");
            fail = 1;
        }
        if (!(caps.features & MEL_CPU_FEATURE_AVX2))
        {
            fprintf(stderr, "FAIL: AVX-512F without AVX2\n");
            fail = 1;
        }
    }
    else if (caps.features & (MEL_CPU_FEATURE_AVX | MEL_CPU_FEATURE_AVX2))
    {
        if (caps.simd_align != 32)
        {
            fprintf(stderr, "FAIL: AVX present but simd_align != 32\n");
            fail = 1;
        }
    }

    if (caps.features & MEL_CPU_FEATURE_AVX2 && !(caps.features & MEL_CPU_FEATURE_AVX))
    {
        fprintf(stderr, "FAIL: AVX2 without AVX\n");
        fail = 1;
    }

#if MEL_CPU_X86 && MEL_ARCH_64BIT
    if (!mel_cpu_has(caps.features, MEL_CPU_FEATURE_SSE2))
    {
        fprintf(stderr, "FAIL: x86-64 must always report SSE2\n");
        fail = 1;
    }
#endif
#if MEL_CPU_ARM && MEL_ARCH_64BIT
    if (!mel_cpu_has(caps.features, MEL_CPU_FEATURE_NEON))
    {
        fprintf(stderr, "FAIL: AArch64 must always report NEON\n");
        fail = 1;
    }
#endif

    Mel_Cpu_Features detected = caps.features;
    if (detected != 0)
    {
        mel_cpu_feature_mask_set(0);
        if (mel_cpu_caps().features != 0)
        {
            fprintf(stderr, "FAIL: zero mask did not disable all features\n");
            fail = 1;
        }
        if (mel_cpu_simd_align() != 0)
        {
            fprintf(stderr, "FAIL: zero mask did not zero simd_align\n");
            fail = 1;
        }
        mel_cpu_feature_mask_set(~(Mel_Cpu_Features)0);
        if (mel_cpu_caps().features != detected)
        {
            fprintf(stderr, "FAIL: restoring mask did not restore detected features\n");
            fail = 1;
        }
    }

    const Mel_Alloc* a = mel_alloc_heap();
    for (u32 align = 16; align <= 64; align <<= 1)
    {
        void* p = mel_cpu_simd_alloc(a, 1024, align);
        if (!p)
        {
            fprintf(stderr, "FAIL: mel_cpu_simd_alloc(align=%u) returned null\n", align);
            fail = 1;
            continue;
        }
        if (((uintptr_t)p & (align - 1)) != 0)
        {
            fprintf(stderr, "FAIL: mel_cpu_simd_alloc(align=%u) misaligned ptr\n", align);
            fail = 1;
        }
        mel_cpu_simd_dealloc(a, p, align);
    }

    if (!fail)
        printf("OK\n");
    return fail;
}
