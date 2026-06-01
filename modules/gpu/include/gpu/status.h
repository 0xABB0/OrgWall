#pragma once

#include <core/types.h>

typedef enum
{
    MEL_GPU_SEVERITY_OK = 0,
    MEL_GPU_SEVERITY_WARNED = 1,
    MEL_GPU_SEVERITY_ERROR = 2,
} Mel_Gpu_Severity;

#define MEL_GPU_SEVERITY_MASK     0x3u

#define MEL_GPU_STATUS(code, sev) (((u32)(code) << 2) | (u32)(sev))

static inline Mel_Gpu_Severity mel_gpu_severity(u32 status) { return (Mel_Gpu_Severity)(status & MEL_GPU_SEVERITY_MASK); }

static inline bool mel_gpu_failed(u32 status) { return (status & MEL_GPU_SEVERITY_MASK) == MEL_GPU_SEVERITY_ERROR; }

static inline bool mel_gpu_warned(u32 status) { return (status & MEL_GPU_SEVERITY_MASK) == MEL_GPU_SEVERITY_WARNED; }

static inline bool mel_gpu_ok(u32 status) { return (status & MEL_GPU_SEVERITY_MASK) == MEL_GPU_SEVERITY_OK; }
