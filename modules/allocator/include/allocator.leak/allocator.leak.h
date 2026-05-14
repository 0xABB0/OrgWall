#pragma once

#include <allocator/fwd.h>
#include <core/types.h>

typedef void (*Mel_Leak_Report_Cb)(const char* file, const char* func, u32 line, usize size, void* user_data);

const Mel_Alloc* mel_alloc_leak_detect(void);
void             mel_leak_dump(Mel_Leak_Report_Cb cb, void* user_data);
