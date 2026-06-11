#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection/array.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_AudioIn_Status;

#define MEL_AUDIOIN_SEVERITY_MASK      0x3u
#define MEL_AUDIOIN_OK                 0u
#define MEL_AUDIOIN_WARNED             1u
#define MEL_AUDIOIN_ERROR              2u

#define MEL_AUDIOIN_RESULT_NO_DEVICE   (1u << 2)
#define MEL_AUDIOIN_RESULT_LOST        (1u << 3)
#define MEL_AUDIOIN_RESULT_DENIED      (1u << 4)
#define MEL_AUDIOIN_RESULT_UNSUPPORTED (1u << 5)

#define MEL_AUDIOIN_WARN_LOCAL_ONLY    (1u << 6)

static inline bool mel_audioin_status_failed(Mel_AudioIn_Status s) { return (s & MEL_AUDIOIN_SEVERITY_MASK) == MEL_AUDIOIN_ERROR; }
static inline bool mel_audioin_status_warned(Mel_AudioIn_Status s) { return (s & MEL_AUDIOIN_SEVERITY_MASK) == MEL_AUDIOIN_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_AudioIn;

#define MEL_AUDIOIN_NULL ((Mel_AudioIn){ 0 })

typedef struct mel_audioin_kind mel_audioin_kind;

extern const mel_audioin_kind mel_audioin_builtin;
extern const mel_audioin_kind mel_audioin_usb;
extern const mel_audioin_kind mel_audioin_bluetooth;
extern const mel_audioin_kind mel_audioin_virtual;
extern const mel_audioin_kind mel_audioin_loopback;
extern const mel_audioin_kind mel_audioin_unknown;

const char* mel_audioin_kind_name(const mel_audioin_kind* k);

typedef struct
{
    bool gain;
} Mel_AudioIn_Caps;

typedef Mel_Array(u32) Mel_AudioIn_Rates;

typedef struct
{
    str8                    name;
    str8                    stable_id;
    const mel_audioin_kind* kind;
    u32                     channels;
    u32                     samplerate;
    Mel_AudioIn_Rates       samplerates;
    Mel_AudioIn_Caps        caps;
    const Mel_Alloc*        alloc;
} Mel_AudioIn_Descriptor;

typedef struct
{
    Mel_AudioIn_Descriptor value;
    Mel_AudioIn_Status     status;
} Mel_AudioIn_Describe_Result;

void mel_audioin_init(const Mel_Alloc* alloc, Mel_Executor* deliver);
void mel_audioin_shutdown(void);

u32         mel_audioin_refresh(void);
u32         mel_audioin_count(void);
u32         mel_audioin_list(Mel_AudioIn* out, u32 cap);
Mel_AudioIn mel_audioin_default(void);
Mel_AudioIn mel_audioin_find(str8 stable_id);

Mel_AudioIn_Describe_Result mel_audioin_describe(Mel_AudioIn d, const Mel_Alloc* a);
void                        mel_audioin_describe_free(Mel_AudioIn_Describe_Result* r);
bool                        mel_audioin_alive(Mel_AudioIn d);
bool                        mel_audioin_equal(Mel_AudioIn a, Mel_AudioIn b);

f32                mel_audioin_gain(Mel_AudioIn d);
Mel_AudioIn_Status mel_audioin_set_gain(Mel_AudioIn d, f32 gain);

#ifdef __cplusplus
}
#endif
