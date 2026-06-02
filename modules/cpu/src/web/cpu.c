#include <cpu/cpu.h>

#include <emscripten/threading.h>

Mel_Cpu_Info mel_cpu_info(void)
{
    Mel_Cpu_Info info = { 0 };
    int          cores = emscripten_num_logical_cores();
    if (cores > 0)
        info.logical_count = (u32)cores;
    info.page_size = 65536u;
    return info;
}
