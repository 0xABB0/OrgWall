#include <cpu/cpu.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MEL_CPU_SYSFS "/sys/devices/system/cpu"

static bool mel_cpu__read_u64(const char* path, u64* out)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned long long v = 0;
    int                got = fscanf(f, "%llu", &v);
    fclose(f);
    if (got != 1)
        return false;
    *out = (u64)v;
    return true;
}

static bool mel_cpu__read_line(const char* path, char* buf, usize cap)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fgets(buf, (int)cap, f) != NULL;
    fclose(f);
    if (!ok)
        return false;
    usize n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return true;
}

static u32 mel_cpu__parse_size(const char* s)
{
    char*              end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end && *end)
    {
        char u = *end;
        if (u == 'K' || u == 'k')
            v *= 1024ULL;
        else if (u == 'M' || u == 'm')
            v *= 1024ULL * 1024ULL;
        else if (u == 'G' || u == 'g')
            v *= 1024ULL * 1024ULL * 1024ULL;
    }
    return (u32)v;
}

static bool mel_cpu__is_leader(const char* cpudir, u64 cpu, const char* primary, const char* legacy)
{
    char path[512];
    u64  first;
    snprintf(path, sizeof path, "%s/topology/%s", cpudir, primary);
    if (mel_cpu__read_u64(path, &first))
        return first == cpu;
    snprintf(path, sizeof path, "%s/topology/%s", cpudir, legacy);
    if (mel_cpu__read_u64(path, &first))
        return first == cpu;
    return false;
}

static void mel_cpu__topology(Mel_Cpu_Info* info)
{
    DIR* d = opendir(MEL_CPU_SYSFS);
    if (!d)
        return;

    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (strncmp(e->d_name, "cpu", 3) != 0 || !isdigit((unsigned char)e->d_name[3]))
            continue;

        u64  cpu = strtoull(e->d_name + 3, NULL, 10);
        char cpudir[300];
        snprintf(cpudir, sizeof cpudir, MEL_CPU_SYSFS "/%s", e->d_name);

        if (mel_cpu__is_leader(cpudir, cpu, "package_cpus_list", "core_siblings_list"))
            info->package_count++;
        if (mel_cpu__is_leader(cpudir, cpu, "core_cpus_list", "thread_siblings_list"))
            info->core_count++;
    }
    closedir(d);
}

static void mel_cpu__caches(Mel_Cpu_Info* info)
{
    for (int i = 0;; i++)
    {
        char base[256];
        snprintf(base, sizeof base, MEL_CPU_SYSFS "/cpu0/cache/index%d", i);

        char path[320];
        u64  level = 0;
        snprintf(path, sizeof path, "%s/level", base);
        if (!mel_cpu__read_u64(path, &level))
            break;

        char type[32] = { 0 };
        snprintf(path, sizeof path, "%s/type", base);
        mel_cpu__read_line(path, type, sizeof type);

        char szstr[32] = { 0 };
        u32  bytes = 0;
        snprintf(path, sizeof path, "%s/size", base);
        if (mel_cpu__read_line(path, szstr, sizeof szstr))
            bytes = mel_cpu__parse_size(szstr);

        if (info->cache_line_size == 0)
        {
            u64 line = 0;
            snprintf(path, sizeof path, "%s/coherency_line_size", base);
            if (mel_cpu__read_u64(path, &line))
                info->cache_line_size = (u32)line;
        }

        bool data = strcmp(type, "Data") == 0;
        bool unified = strcmp(type, "Unified") == 0;
        if (level == 1 && (data || (unified && info->l1_cache_size == 0)))
            info->l1_cache_size = bytes;
        else if (level == 2 && info->l2_cache_size == 0)
            info->l2_cache_size = bytes;
        else if (level == 3 && info->l3_cache_size == 0)
            info->l3_cache_size = bytes;
    }
}

static u64 mel_cpu__proc_mhz(void)
{
    FILE* f = fopen("/proc/cpuinfo", "rb");
    if (!f)
        return 0;
    char line[256];
    u64  hz = 0;
    while (fgets(line, sizeof line, f))
    {
        if (strncmp(line, "cpu MHz", 7) == 0)
        {
            char* colon = strchr(line, ':');
            if (colon)
                hz = (u64)(strtod(colon + 1, NULL) * 1e6);
            break;
        }
    }
    fclose(f);
    return hz;
}

Mel_Cpu_Info mel_cpu_info(void)
{
    Mel_Cpu_Info info = { 0 };

    long logical = sysconf(_SC_NPROCESSORS_CONF);
    if (logical > 0)
        info.logical_count = (u32)logical;
    long page = sysconf(_SC_PAGESIZE);
    if (page > 0)
        info.page_size = (u32)page;

    mel_cpu__topology(&info);
    mel_cpu__caches(&info);

    u64 khz = 0;
    if (mel_cpu__read_u64(MEL_CPU_SYSFS "/cpu0/cpufreq/cpuinfo_max_freq", &khz))
        info.clock_speed = khz * 1000ULL;
    else
        info.clock_speed = mel_cpu__proc_mhz();

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
    if (info.cache_line_size == 0)
    {
        long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        if (line > 0)
            info.cache_line_size = (u32)line;
    }
#endif

    return info;
}

#include "../../src/cpu_internal.h"
#include "../../src/cpu_x86.h"

#if MEL_CPU_ARM
#include <sys/auxv.h>
#ifndef HWCAP_NEON
#define HWCAP_NEON (1u << 12)
#endif
#endif

u64 mel_cpu__ram_total(void)
{
    long pages = sysconf(_SC_PHYS_PAGES);
    long psz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psz > 0)
        return (u64)pages * (u64)psz;
    return 0;
}

Mel_Cpu_Features mel_cpu__detect_features(void)
{
#if MEL_CPU_X86
    return mel_cpu__detect_x86();
#elif MEL_CPU_ARM
    Mel_Cpu_Features f = 0;
#if MEL_ARCH_64BIT
    f |= MEL_CPU_FEATURE_NEON;
#else
    unsigned long hwcap = getauxval(AT_HWCAP);
    if (hwcap & HWCAP_NEON)
        f |= MEL_CPU_FEATURE_NEON;
#endif
    return f;
#else
    return 0;
#endif
}
