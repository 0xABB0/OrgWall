#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Locale_Status;

#define MEL_LOCALE_SEVERITY_MASK 0x3u
#define MEL_LOCALE_OK            0u
#define MEL_LOCALE_WARNED        1u
#define MEL_LOCALE_ERROR         2u

#define MEL_LOCALE_EMPTY          (1u << 2)
#define MEL_LOCALE_NO_COUNTRY     (1u << 3)
#define MEL_LOCALE_TAG_NORMALIZED (1u << 4)
#define MEL_LOCALE_UNAVAILABLE    (1u << 5)
#define MEL_LOCALE_OUT_OF_RANGE   (1u << 6)

static inline bool mel_locale_status_failed(Mel_Locale_Status s) { return (s & MEL_LOCALE_SEVERITY_MASK) == MEL_LOCALE_ERROR; }
static inline bool mel_locale_status_warned(Mel_Locale_Status s) { return (s & MEL_LOCALE_SEVERITY_MASK) == MEL_LOCALE_WARNED; }
static inline bool mel_locale_status_ok(Mel_Locale_Status s) { return (s & MEL_LOCALE_SEVERITY_MASK) == MEL_LOCALE_OK; }
static inline bool mel_locale_status_empty(Mel_Locale_Status s) { return (s & MEL_LOCALE_EMPTY) != 0u; }
static inline bool mel_locale_status_no_country(Mel_Locale_Status s) { return (s & MEL_LOCALE_NO_COUNTRY) != 0u; }

typedef struct
{
    str8 tag;
    str8 language;
    str8 country;
} Mel_Locale;

static inline bool mel_locale_has_country(Mel_Locale l) { return l.country.len > 0; }

typedef struct
{
    Mel_Locale        value;
    Mel_Locale_Status status;
} Mel_Locale_Get_Result;

typedef struct Mel_Executor Mel_Executor;

void mel_locale_init(const Mel_Alloc* alloc);
void mel_locale_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_locale_shutdown(void);

u32 mel_locale_refresh(void);
u32 mel_locale_count(void);
u32 mel_locale_list(Mel_Locale* out, u32 cap);

Mel_Locale_Get_Result mel_locale_at(u32 index);
Mel_Locale_Get_Result mel_locale_primary(void);

bool mel_locale_equal(Mel_Locale a, Mel_Locale b);

#ifdef __cplusplus
}
#endif
