#include <cpu/cpu.h>

#include "cpu_internal.h"

#include <allocator/allocator.h>

static Mel_Cpu_Features mel_cpu__feature_mask = ~(Mel_Cpu_Features)0;

void mel_cpu_feature_mask_set(Mel_Cpu_Features allowed)
{
    mel_cpu__feature_mask = allowed;
}

Mel_Cpu_Features mel_cpu_feature_mask_get(void)
{
    return mel_cpu__feature_mask;
}

static Mel_Cpu_Features mel_cpu__features_effective(void)
{
    return mel_cpu__detect_features() & mel_cpu__feature_mask;
}

static u32 mel_cpu__align_for(Mel_Cpu_Features f)
{
    if (f & MEL_CPU_FEATURE_AVX512F)
        return 64u;
    if (f & (MEL_CPU_FEATURE_AVX | MEL_CPU_FEATURE_AVX2))
        return 32u;
    if (f & (MEL_CPU_FEATURE_SSE | MEL_CPU_FEATURE_SSE2 | MEL_CPU_FEATURE_SSE3 | MEL_CPU_FEATURE_SSSE3 | MEL_CPU_FEATURE_SSE41 | MEL_CPU_FEATURE_SSE42 | MEL_CPU_FEATURE_NEON))
        return 16u;
    return 0u;
}

u32 mel_cpu_simd_align(void)
{
    return mel_cpu__align_for(mel_cpu__features_effective());
}

u64 mel_cpu_ram_total(void)
{
    return mel_cpu__ram_total();
}

Mel_Cpu_Caps mel_cpu_caps(void)
{
    Mel_Cpu_Caps caps = { 0 };
    caps.features = mel_cpu__features_effective();
    caps.ram_total = mel_cpu__ram_total();
    caps.simd_align = mel_cpu__align_for(caps.features);
    return caps;
}

void* mel_cpu_simd_alloc(const Mel_Alloc* alloc, usize size, u32 align)
{
    assert(alloc != NULL);
    assert(size > 0);
    assert(align > 0 && (align & (align - 1)) == 0);
    return mel_aligned_alloc(alloc, size, align);
}

void mel_cpu_simd_dealloc(const Mel_Alloc* alloc, void* ptr, u32 align)
{
    assert(alloc != NULL);
    mel_aligned_dealloc(alloc, ptr, align);
}
