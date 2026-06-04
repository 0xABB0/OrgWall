#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u32 package_count;
    u32 core_count;
    u32 logical_count;
    u32 l1_cache_size;
    u32 l2_cache_size;
    u32 l3_cache_size;
    u32 page_size;
    u64 clock_speed;
    u32 cache_line_size;
} Mel_Cpu_Info;

MEL_NODISCARD Mel_Cpu_Info mel_cpu_info(void);

typedef u64 Mel_Cpu_Features;

enum
{
    MEL_CPU_FEATURE_SSE = 1ull << 0,
    MEL_CPU_FEATURE_SSE2 = 1ull << 1,
    MEL_CPU_FEATURE_SSE3 = 1ull << 2,
    MEL_CPU_FEATURE_SSSE3 = 1ull << 3,
    MEL_CPU_FEATURE_SSE41 = 1ull << 4,
    MEL_CPU_FEATURE_SSE42 = 1ull << 5,
    MEL_CPU_FEATURE_AVX = 1ull << 6,
    MEL_CPU_FEATURE_AVX2 = 1ull << 7,
    MEL_CPU_FEATURE_AVX512F = 1ull << 8,
    MEL_CPU_FEATURE_NEON = 1ull << 9,
};

typedef struct
{
    Mel_Cpu_Features features;
    u64              ram_total;
    u32              simd_align;
} Mel_Cpu_Caps;

MEL_NODISCARD Mel_Cpu_Caps mel_cpu_caps(void);

MEL_NODISCARD static inline bool mel_cpu_has(Mel_Cpu_Features set, Mel_Cpu_Features want)
{
    return (set & want) == want;
}

void                           mel_cpu_feature_mask_set(Mel_Cpu_Features allowed);
MEL_NODISCARD Mel_Cpu_Features mel_cpu_feature_mask_get(void);

MEL_NODISCARD u64 mel_cpu_ram_total(void);

MEL_NODISCARD u32 mel_cpu_simd_align(void);

MEL_NODISCARD MEL_ALLOC_SIZE(2) MEL_ALLOC_ALIGN(3) void* mel_cpu_simd_alloc(const Mel_Alloc* alloc, usize size, u32 align);
void mel_cpu_simd_dealloc(const Mel_Alloc* alloc, void* ptr, u32 align);

#ifdef __cplusplus
}
#endif
