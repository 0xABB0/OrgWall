#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Repl Mel_Repl;

typedef struct
{
    bool             ok;
    char*            text;        // printable value, or NULL
    char*            diagnostics; // error text, or NULL
    const Mel_Alloc* alloc;       // owns text + diagnostics (set by the language), or NULL
} Mel_Repl_Result;

// A language plugged into the loop. `self` holds the language's session
// (for the C language: a Mel_Jit plus accumulated declarations). The loop is
// agnostic — it never sees jit, llvm, or LLVM IR.
//
// `complete` is the optional continuation predicate: given the bytes accumulated
// so far across one or more physical lines, it answers whether the fragment is a
// terminated unit ready to eval. NULL means every physical line is complete (no
// continuation). The loop never invents a default classifier; absence is honest.
typedef struct
{
    void* self;
    Mel_Repl_Result (*eval)(void* self, str8 line, const Mel_Alloc* alloc);
    bool (*complete)(void* self, str8 accumulated);
    void (*destroy)(void* self);
} Mel_Repl_Lang;

// Platform-neutral line source. `read` yields the next physical line (newline
// stripped) into `*out`, whose bytes the source owns and keeps valid until the
// next `read` or `destroy`. Returns false at end-of-input. `self`/`destroy` mirror
// the language vtable. A scripted in-memory source drives tests; a stdio source
// drives a terminal.
typedef struct
{
    void* self;
    bool (*read)(void* self, str8* out);
    void (*destroy)(void* self);
} Mel_Repl_Source;

// Platform-neutral output sink. `write` receives prompts, echoes, results, and
// diagnostics as raw byte spans (no implicit newline). A capturing sink drives
// tests; a stdio sink drives a terminal.
typedef struct
{
    void* self;
    void (*write)(void* self, str8 bytes);
    void (*destroy)(void* self);
} Mel_Repl_Sink;

// Prompts the loop emits. `primary` precedes a fresh unit; `continuation`
// precedes each subsequent physical line of an unterminated unit. Both are
// borrowed for the call's duration.
typedef struct
{
    str8 primary;
    str8 continuation;
} Mel_Repl_Prompts;

Mel_Repl* mel_repl_create(const Mel_Alloc* allocator, Mel_Repl_Lang lang);
void      mel_repl_destroy(Mel_Repl* repl);

Mel_Repl_Result mel_repl_eval(Mel_Repl* repl, str8 line);
// Self-describing free: uses the allocator the result carries.
void            mel_repl_result_free(Mel_Repl_Result* r);

// Drive the read-eval-print loop until the source signals end-of-input. Each
// iteration: emit a prompt, read a physical line, accumulate until `lang.complete`
// (when present) accepts the fragment, dispatch via `mel_repl_eval`, emit the
// result's text or diagnostics, and record the input in history. Returns the count
// of units evaluated. `source` and `sink` are required; `prompts` is required.
usize mel_repl_run(Mel_Repl* repl, Mel_Repl_Source source, Mel_Repl_Sink sink, Mel_Repl_Prompts prompts);

// History of evaluated inputs (one str8 per dispatched unit), owned by the repl's
// allocator. Borrowed; valid until mel_repl_destroy. `*count` receives the length.
const str8* mel_repl_history(const Mel_Repl* repl, usize* count);

#ifdef __cplusplus
}
#endif
