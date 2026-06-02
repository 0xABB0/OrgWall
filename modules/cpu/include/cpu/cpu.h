#pragma once

#include <core/types.h>
#include <core/compiler.h>

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
