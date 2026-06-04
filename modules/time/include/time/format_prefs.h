#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Time_Format_Status;

#define MEL_TIME_FMT_SEVERITY_MASK 0x3u
#define MEL_TIME_FMT_OK            0u
#define MEL_TIME_FMT_WARNED        1u
#define MEL_TIME_FMT_ERROR         2u

#define MEL_TIME_FMT_UNAVAILABLE   (1u << 2)
#define MEL_TIME_FMT_ORDER_GUESSED (1u << 3)
#define MEL_TIME_FMT_CLOCK_GUESSED (1u << 4)

static inline bool mel_time_fmt_status_ok(Mel_Time_Format_Status s) { return (s & MEL_TIME_FMT_SEVERITY_MASK) == MEL_TIME_FMT_OK; }
static inline bool mel_time_fmt_status_warned(Mel_Time_Format_Status s) { return (s & MEL_TIME_FMT_SEVERITY_MASK) == MEL_TIME_FMT_WARNED; }
static inline bool mel_time_fmt_status_failed(Mel_Time_Format_Status s) { return (s & MEL_TIME_FMT_SEVERITY_MASK) == MEL_TIME_FMT_ERROR; }
static inline bool mel_time_fmt_status_unavailable(Mel_Time_Format_Status s) { return (s & MEL_TIME_FMT_UNAVAILABLE) != 0u; }

enum
{
    MEL_DATE_ORDER_YMD = 1u << 0,
    MEL_DATE_ORDER_DMY = 1u << 1,
    MEL_DATE_ORDER_MDY = 1u << 2,
};

enum
{
    MEL_CLOCK_24H = 1u << 0,
    MEL_CLOCK_12H = 1u << 1,
};

typedef struct
{
    u32  date_order;
    u32  clock;
    char date_separator[4];
} Mel_Time_Format_Prefs;

typedef struct
{
    Mel_Time_Format_Prefs  value;
    Mel_Time_Format_Status status;
} Mel_Time_Format_Result;

void mel_time_format_init(const Mel_Alloc* alloc);
void mel_time_format_shutdown(void);

Mel_Time_Format_Status mel_time_format_refresh(void);
Mel_Time_Format_Result mel_time_format_prefs(void);

usize mel_time_format_date(Mel_Time_Format_Prefs p, i32 year, u32 month, u32 day, char* out, usize cap);

#ifdef __cplusplus
}
#endif
