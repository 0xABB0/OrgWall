#include <cpu/cpu.h>

#include <sys/sysctl.h>

static u64 mel_cpu__sysctl(const char* name)
{
    u64    v = 0;
    size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, NULL, 0) != 0)
        return 0;
    return v;
}

Mel_Cpu_Info mel_cpu_info(void)
{
    Mel_Cpu_Info info = { 0 };
    info.package_count = (u32)mel_cpu__sysctl("hw.packages");
    info.core_count = (u32)mel_cpu__sysctl("hw.physicalcpu");
    info.logical_count = (u32)mel_cpu__sysctl("hw.logicalcpu");
    info.l1_cache_size = (u32)mel_cpu__sysctl("hw.l1dcachesize");
    info.l2_cache_size = (u32)mel_cpu__sysctl("hw.l2cachesize");
    info.l3_cache_size = (u32)mel_cpu__sysctl("hw.l3cachesize");
    info.page_size = (u32)mel_cpu__sysctl("hw.pagesize");
    info.clock_speed = mel_cpu__sysctl("hw.cpufrequency");
    info.cache_line_size = (u32)mel_cpu__sysctl("hw.cachelinesize");
    return info;
}

#include "../../src/cpu_internal.h"
#include "../../src/cpu_x86.h"

u64 mel_cpu__ram_total(void)
{
    return mel_cpu__sysctl("hw.memsize");
}

Mel_Cpu_Features mel_cpu__detect_features(void)
{
#if MEL_CPU_X86
    return mel_cpu__detect_x86();
#elif MEL_CPU_ARM
    Mel_Cpu_Features f = 0;
    if (mel_cpu__sysctl("hw.optional.neon") != 0 || mel_cpu__sysctl("hw.optional.AdvSIMD") != 0)
        f |= MEL_CPU_FEATURE_NEON;
    return f;
#else
    return 0;
#endif
}
