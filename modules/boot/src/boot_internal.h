#pragma once

#include <allocator/allocator.fwd.h>

typedef struct Mel_Vat Mel_Vat;

void mel_boot__init(int argc, char** argv, const Mel_Alloc* alloc);
int  mel_boot__finish(void);
void mel_boot__lifecycle_init(Mel_Vat* vat, const Mel_Alloc* alloc);
void mel_boot__lifecycle_shutdown(void);
void mel_boot__lifecycle_platform_start(void);
void mel_boot__lifecycle_platform_stop(void);
