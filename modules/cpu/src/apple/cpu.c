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

Mel_Cpu_Info mel_cpu_info(const Mel_Alloc* alloc)
{
    MEL_UNUSED(alloc);

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
