#include <cpu/cpu.h>

#include <allocator/allocator.h>

#include <windows.h>

static void mel_cpu__topology(Mel_Cpu_Info* info, const Mel_Alloc* alloc)
{
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, NULL, &len);
    if (len == 0)
        return;

    BYTE* buf = (BYTE*)mel_alloc(alloc, len);
    if (!buf)
        return;

    if (GetLogicalProcessorInformationEx(RelationAll, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buf, &len))
    {
        BYTE* p = buf;
        BYTE* end = buf + len;
        while (p < end)
        {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* rec = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)p;
            switch (rec->Relationship)
            {
            case RelationProcessorCore:
                info->core_count++;
                break;
            case RelationProcessorPackage:
                info->package_count++;
                break;
            case RelationCache:
            {
                CACHE_RELATIONSHIP* c = &rec->Cache;
                if (info->cache_line_size == 0 && c->LineSize)
                    info->cache_line_size = c->LineSize;
                bool usable = (c->Type == CacheData || c->Type == CacheUnified);
                if (c->Level == 1 && usable && info->l1_cache_size == 0)
                    info->l1_cache_size = (u32)c->CacheSize;
                else if (c->Level == 2 && info->l2_cache_size == 0)
                    info->l2_cache_size = (u32)c->CacheSize;
                else if (c->Level == 3 && info->l3_cache_size == 0)
                    info->l3_cache_size = (u32)c->CacheSize;
                break;
            }
            default:
                break;
            }
            p += rec->Size;
        }
    }

    mel_dealloc(alloc, buf);
}

Mel_Cpu_Info mel_cpu_info(const Mel_Alloc* alloc)
{
    Mel_Cpu_Info info = { 0 };

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    info.page_size = (u32)si.dwPageSize;

    DWORD logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    info.logical_count = logical ? (u32)logical : (u32)si.dwNumberOfProcessors;

    mel_cpu__topology(&info, alloc);

    DWORD mhz = 0;
    DWORD sz = sizeof mhz;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "~MHz", RRF_RT_REG_DWORD, NULL, &mhz, &sz) == ERROR_SUCCESS)
        info.clock_speed = (u64)mhz * 1000000ULL;

    return info;
}
