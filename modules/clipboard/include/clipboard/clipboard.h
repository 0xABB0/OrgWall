#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection.array/array.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor  Mel_Reactor;
typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Future   Mel_Future;
typedef struct Mel_Event    Mel_Event;

typedef u32 Mel_Clip_Status;

#define MEL_CLIP_SEVERITY_MASK               0x3u
#define MEL_CLIP_OK                          0u
#define MEL_CLIP_WARNED                      1u
#define MEL_CLIP_ERROR                       2u

#define MEL_CLIP_RESULT_DENIED               (1u << 2)
#define MEL_CLIP_RESULT_NO_CLIPBOARD         (1u << 3)
#define MEL_CLIP_RESULT_EMPTY                (1u << 4)
#define MEL_CLIP_RESULT_CANCELLED            (1u << 5)
#define MEL_CLIP_RESULT_STALE                (1u << 6)

#define MEL_CLIP_WARN_FORMAT_UNAVAILABLE     (1u << 8)
#define MEL_CLIP_WARN_REPRESENTATION_DROPPED (1u << 9)
#define MEL_CLIP_WARN_TRANSCODED             (1u << 10)
#define MEL_CLIP_WARN_TRUNCATED              (1u << 11)

static inline bool mel_clip_failed(Mel_Clip_Status s) { return (s & MEL_CLIP_SEVERITY_MASK) == MEL_CLIP_ERROR; }
static inline bool mel_clip_warned(Mel_Clip_Status s) { return (s & MEL_CLIP_SEVERITY_MASK) == MEL_CLIP_WARNED; }

typedef u32 Mel_Clip_Format;

#define MEL_CLIP_FMT_NONE     0u
#define MEL_CLIP_FMT_TEXT     1u
#define MEL_CLIP_FMT_HTML     2u
#define MEL_CLIP_FMT_PNG      3u
#define MEL_CLIP_FMT_URI_LIST 4u
#define MEL_CLIP_FMT_RTF      5u

typedef struct
{
    Mel_Clip_Format format;
    str8            bytes;
} Mel_Clip_Rep;

typedef struct
{
    Mel_Array(Mel_Clip_Rep) reps;
} Mel_Clip_Item;

typedef struct
{
    Mel_Array(Mel_Clip_Item) items;
    const Mel_Alloc* alloc;
} Mel_Clip_Transferable;

typedef struct
{
    const Mel_Clip_Format* items;
    u32                    count;
} Mel_Clip_Formats;

typedef struct
{
    Mel_Executor*    exec;
    const Mel_Alloc* alloc;
} Mel_Clip_Opt;

void mel_clip_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_clip_shutdown(void);
bool mel_clip_available(void);

Mel_Clip_Format mel_clip_format_register(str8 mime);
str8            mel_clip_format_mime(Mel_Clip_Format f);

void           mel_clip_transferable_init(Mel_Clip_Transferable* t, const Mel_Alloc* alloc);
void           mel_clip_transferable_free(Mel_Clip_Transferable* t);
Mel_Clip_Item* mel_clip_item_add(Mel_Clip_Transferable* t);
void           mel_clip_rep_add(Mel_Clip_Item* it, Mel_Clip_Format f, str8 bytes, const Mel_Alloc* alloc);

Mel_Future* mel_clip_read_opt(const Mel_Clip_Format* fmts, u32 n, Mel_Clip_Opt opt);
Mel_Future* mel_clip_write_opt(const Mel_Clip_Transferable* t, Mel_Clip_Opt opt);
Mel_Future* mel_clip_query_opt(Mel_Clip_Opt opt);
Mel_Future* mel_clip_clear_opt(Mel_Clip_Opt opt);

Mel_Future* mel_clip_read_text_opt(Mel_Clip_Opt opt);
Mel_Future* mel_clip_write_text_opt(str8 text, Mel_Clip_Opt opt);

#define mel_clip_read(fmts, n, ...)    mel_clip_read_opt((fmts), (n), (Mel_Clip_Opt){ __VA_ARGS__ })
#define mel_clip_write(t, ...)         mel_clip_write_opt((t), (Mel_Clip_Opt){ __VA_ARGS__ })
#define mel_clip_query(...)            mel_clip_query_opt((Mel_Clip_Opt){ __VA_ARGS__ })
#define mel_clip_clear(...)            mel_clip_clear_opt((Mel_Clip_Opt){ __VA_ARGS__ })
#define mel_clip_read_text(...)        mel_clip_read_text_opt((Mel_Clip_Opt){ __VA_ARGS__ })
#define mel_clip_write_text(text, ...) mel_clip_write_text_opt((text), (Mel_Clip_Opt){ __VA_ARGS__ })

Mel_Clip_Status              mel_clip_future_status(const Mel_Future* f);
const Mel_Clip_Transferable* mel_clip_future_transferable(const Mel_Future* f);
str8                         mel_clip_future_text(const Mel_Future* f);
Mel_Clip_Formats             mel_clip_future_formats(const Mel_Future* f);
void                         mel_clip_future_free(Mel_Future* f);

u64        mel_clip_sequence(void);
Mel_Event* mel_clip_watch_opt(Mel_Clip_Opt opt);
void       mel_clip_unwatch(void);
#define mel_clip_watch(...) mel_clip_watch_opt((Mel_Clip_Opt){ __VA_ARGS__ })

void* mel_clip_native(void);

#ifdef __cplusplus
}
#endif
