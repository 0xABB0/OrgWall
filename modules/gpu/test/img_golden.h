#pragma once

#include <core/types.h>

#include <test/test.h>

typedef struct
{
    u8   max_channel_delta;
    f32  max_fraction_exceeding;
    bool assert_opaque_alpha;
} Mel_Golden_Tolerance;

typedef struct
{
    bool pass;
    char message[512];
} Mel_Golden_Result;

bool mel_golden_update_requested(void);

Mel_Golden_Result mel_golden_compare(const char* backend, const char* name, const u8* produced_rgba, u32 width, u32 height, Mel_Golden_Tolerance tol);

bool mel_golden_check(const char* backend, const char* name, const u8* produced_rgba, u32 width, u32 height, Mel_Golden_Tolerance tol, const char* file, int line);

#define MEL_GOLDEN(backend_, name_, rgba_, w_, h_, tol_)                                             \
    do                                                                                               \
    {                                                                                                \
        if (!mel_golden_check((backend_), (name_), (rgba_), (w_), (h_), (tol_), __FILE__, __LINE__)) \
            mel_test_abort();                                                                        \
    } while (0)
