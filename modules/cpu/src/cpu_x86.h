#pragma once

#include <cpu/cpu.h>
#include <core/platform.h>

#if MEL_CPU_X86

#if MEL_COMPILER_MSVC
#include <intrin.h>
static inline void mel_cpu__cpuid(u32 leaf, u32 subleaf, u32 regs[4])
{
    int r[4];
    __cpuidex(r, (int)leaf, (int)subleaf);
    regs[0] = (u32)r[0];
    regs[1] = (u32)r[1];
    regs[2] = (u32)r[2];
    regs[3] = (u32)r[3];
}
static inline u64 mel_cpu__xgetbv0(void)
{
    return _xgetbv(0);
}
#else
#include <cpuid.h>
static inline void mel_cpu__cpuid(u32 leaf, u32 subleaf, u32 regs[4])
{
    __cpuid_count(leaf, subleaf, regs[0], regs[1], regs[2], regs[3]);
}
static inline u64 mel_cpu__xgetbv0(void)
{
    u32 lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((u64)hi << 32) | lo;
}
#endif

static inline Mel_Cpu_Features mel_cpu__detect_x86(void)
{
    Mel_Cpu_Features f = 0;

    u32 r[4] = { 0 };
    mel_cpu__cpuid(0, 0, r);
    u32 max_leaf = r[0];
    if (max_leaf < 1)
        return f;

    mel_cpu__cpuid(1, 0, r);
    u32 ecx1 = r[2];
    u32 edx1 = r[3];

    if (edx1 & (1u << 25))
        f |= MEL_CPU_FEATURE_SSE;
    if (edx1 & (1u << 26))
        f |= MEL_CPU_FEATURE_SSE2;
    if (ecx1 & (1u << 0))
        f |= MEL_CPU_FEATURE_SSE3;
    if (ecx1 & (1u << 9))
        f |= MEL_CPU_FEATURE_SSSE3;
    if (ecx1 & (1u << 19))
        f |= MEL_CPU_FEATURE_SSE41;
    if (ecx1 & (1u << 20))
        f |= MEL_CPU_FEATURE_SSE42;

    bool osxsave = (ecx1 & (1u << 27)) != 0;
    bool avx_cpu = (ecx1 & (1u << 28)) != 0;
    bool ymm_ok = false;
    bool zmm_ok = false;
    if (osxsave)
    {
        u64 xcr0 = mel_cpu__xgetbv0();
        ymm_ok = (xcr0 & 0x6u) == 0x6u;
        zmm_ok = ymm_ok && (xcr0 & 0xe0u) == 0xe0u;
    }
    if (avx_cpu && ymm_ok)
        f |= MEL_CPU_FEATURE_AVX;

    if (max_leaf >= 7)
    {
        u32 r7[4] = { 0 };
        mel_cpu__cpuid(7, 0, r7);
        u32 ebx7 = r7[1];
        if ((ebx7 & (1u << 5)) && ymm_ok)
            f |= MEL_CPU_FEATURE_AVX2;
        if ((ebx7 & (1u << 16)) && zmm_ok)
            f |= MEL_CPU_FEATURE_AVX512F;
    }

    return f;
}

#endif
