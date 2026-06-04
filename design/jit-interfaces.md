# JIT — module interface contracts

The four public surfaces. `jit` and `repl` are pure C interfaces (no LLVM); `llvm` and `clang` are C-over-C++ shims. No enums (MEL-CODE-001): pluggability is by vtable.

## `jit` — `<jit/jit.h>`
```c
typedef struct Mel_Jit_Module Mel_Jit_Module;   // opaque LLVM IR module; defined by `llvm`
typedef struct Mel_Jit        Mel_Jit;

typedef struct { u32 index; u32 generation; } Mel_Jit_Unit;   // {0} == null unit
// Self-describing: `alloc` owns `diagnostics`, set by the producer; freed via mel_jit_result_free.
typedef struct { bool ok; char* diagnostics; const Mel_Alloc* alloc; } Mel_Jit_Result;

// Execution backend. `self` is the backend instance; the façade dispatches through these.
typedef struct {
    void*          self;
    Mel_Jit_Unit   (*add)(void* self, Mel_Jit_Module* module, Mel_Jit_Result* out);
    Mel_Jit_Result (*replace)(void* self, Mel_Jit_Unit unit, Mel_Jit_Module* module);
    void           (*remove)(void* self, Mel_Jit_Unit unit);
    void*          (*lookup)(void* self, const char* symbol);
    void           (*define_symbol)(void* self, const char* symbol, void* address);
    void           (*destroy)(void* self);
} Mel_Jit_Backend;

Mel_Jit* mel_jit_create(const Mel_Alloc* allocator, Mel_Jit_Backend backend);
void     mel_jit_destroy(Mel_Jit* jit);                          // calls backend.destroy

Mel_Jit_Unit   mel_jit_add(Mel_Jit* jit, Mel_Jit_Module* module, Mel_Jit_Result* out);
Mel_Jit_Result mel_jit_replace(Mel_Jit* jit, Mel_Jit_Unit unit, Mel_Jit_Module* module);
void           mel_jit_remove(Mel_Jit* jit, Mel_Jit_Unit unit);
void*          mel_jit_lookup(Mel_Jit* jit, const char* symbol);
void           mel_jit_define_symbol(Mel_Jit* jit, const char* symbol, void* address);
void           mel_jit_result_free(Mel_Jit_Result* result);   // frees via result->alloc
```
The façade owns the allocator + the backend value; every call is a thin dispatch. `Mel_Jit_Module` ownership transfers to the backend on a successful `add`/`replace` (the backend consumes/frees the IR). Results are self-describing — each carries the allocator that owns its strings, so freeing never depends on which façade produced it (decouples backend and façade allocators).

## `repl` — `<repl/repl.h>`
```c
typedef struct Mel_Repl Mel_Repl;
typedef struct { bool ok; char* text; char* diagnostics; const Mel_Alloc* alloc; } Mel_Repl_Result;

// A language plugged into the loop. `self` holds the language's session (e.g. a Mel_Jit + decls).
// `complete` is the optional continuation predicate over the bytes accumulated so far across one
// or more physical lines; NULL means every physical line is a complete unit (no continuation).
typedef struct {
    void*           self;
    Mel_Repl_Result (*eval)(void* self, str8 line, const Mel_Alloc* alloc);
    bool            (*complete)(void* self, str8 accumulated);
    void            (*destroy)(void* self);
} Mel_Repl_Lang;

// Line source / output sink — the loop's only I/O, both vtables so tests drive them in-memory
// (scripted source, capturing sink) and a terminal driver supplies stdio versions. `read` yields
// the next physical line (newline stripped) into `*out`, source-owned, valid until the next read;
// false at EOF. `write` takes raw byte spans (the loop appends newlines explicitly).
typedef struct { void* self; bool (*read)(void* self, str8* out); void (*destroy)(void* self); } Mel_Repl_Source;
typedef struct { void* self; void (*write)(void* self, str8 bytes); void (*destroy)(void* self); } Mel_Repl_Sink;
typedef struct { str8 primary; str8 continuation; } Mel_Repl_Prompts;

Mel_Repl*       mel_repl_create(const Mel_Alloc* allocator, Mel_Repl_Lang lang);
void            mel_repl_destroy(Mel_Repl* repl);
Mel_Repl_Result mel_repl_eval(Mel_Repl* repl, str8 line);   // dispatch + record history
void            mel_repl_result_free(Mel_Repl_Result* r);   // self-describing, frees via r->alloc

// The loop: prompt -> read -> accumulate until `lang.complete` accepts (or EOF) -> dispatch -> emit
// text/diagnostics -> record history. Returns the count of units evaluated. EOF mid-unit dispatches
// the unterminated fragment with a loud warning (never a silent drop — MEL-ENGINE-VIII).
usize           mel_repl_run(Mel_Repl* repl, Mel_Repl_Source source, Mel_Repl_Sink sink, Mel_Repl_Prompts prompts);
// History of dispatched inputs (one str8 per unit), allocator-owned, borrowed until destroy.
const str8*     mel_repl_history(const Mel_Repl* repl, usize* count);
```
`repl` never sees `jit` or `Mel_Jit_Module`; a language frontend uses a backend privately inside `self`.
History accumulates each dispatched unit (multi-line units joined by `\n`) in a `Mel_Array(str8)` owned
by the repl's allocator. The loop holds no fixed buffers: the per-unit accumulator is a `Mel_Array(u8)`.

## `llvm` — `<llvm/llvm.h>`
```c
typedef struct { u32 opt_level; bool expose_process_symbols; } Mel_Llvm_Orc_Config;

// IR production (the custom-language entry today; an IR-builder surface follows).
Mel_Jit_Module* mel_llvm_parse_ir(const Mel_Alloc* a, str8 name, str8 ir_text, Mel_Jit_Result* out);
void            mel_llvm_module_free(const Mel_Alloc* a, Mel_Jit_Module* module);

// ORC implementation of Mel_Jit_Backend. Plug into mel_jit_create.
Mel_Jit_Backend mel_llvm_orc_backend(const Mel_Alloc* a, const Mel_Llvm_Orc_Config* cfg);
```
`struct Mel_Jit_Module` is defined here, wrapping an `LLVMOrcThreadSafeModule`. The `clang`
frontend feeds the backend through `mel_llvm_parse_ir` (IR text), never by handing over a raw
`llvm::Module`: clang's IR is built inside `libclang-cpp`'s LLVM, the ORC backend runs on the
statically-linked LLVM, and the two copies' weak template instantiations coalesce destructively
under dyld — serialising to IR text and re-parsing keeps every module that reaches ORC native to
the backend's LLVM (the dual-LLVM hazard noted in `design/jit-vendor-llvm.md`). The ORC backend: `add` = fresh `ResourceTracker` + `addIRModule`; `replace` = `tracker->remove` then re-add; `lookup` = `LLJIT::lookup`; `define_symbol` = `absoluteSymbols`; `expose_process_symbols` toggles the process-symbol generator.

## `clang` — `<clang/clang.h>`
```c
typedef struct { u32 opt_level; } Mel_Clang_Config;   // host triple + c23 by default

// C source -> LLVM IR module (via `llvm`).
Mel_Jit_Module* mel_clang_compile(const Mel_Alloc* a, str8 name, str8 source,
                                  const Mel_Clang_Config* cfg, Mel_Jit_Result* out);

// C as a REPL language, backed by clang::Interpreter (incremental; typed values; persistent
// globals). The Interpreter owns its ORC JIT against the shared libLLVM; this path does NOT use
// the pluggable `jit` backend. Runs in C++ mode (accepts the user's C as C++).
Mel_Repl_Lang mel_clang_repl_lang(const Mel_Alloc* a);
```
Pipeline inside `mel_clang_compile`: `CompilerInstance` (host triple, c23; `-resource-dir` located at
runtime via the loaded `libclang-cpp` image) → in-memory `MemoryBuffer` named `name` remapped onto the
input → a `clang::ASTFrontendAction` whose `ASTConsumer` is `CreateLLVMCodeGen` (raw IR emission, **no
backend pass pipeline** — running clang's new-PM pipeline trips the dual-LLVM weak-symbol coalescing
crash) → the `llvm::Module` is printed to IR text → `mel_llvm_parse_ir` re-parses it on the backend's
LLVM. The shim is compiled `-fno-rtti` to match clang/LLVM's ABI. Diagnostics captured to `out` via a
`TextDiagnosticPrinter` over a string ostream. `Mel_Clang_Config.opt_level` feeds the cc1 `-On` flag;
the ORC backend applies the actual optimization pipeline (its own `opt_level`).

## Skeleton vs wired
Skeleton: `jit`/`repl` are fully real (tested with mock vtables). `llvm`/`clang` shims compile with **no toolchain dependency** and loud-stub every entry (MEL-ENGINE-VIII) until vendoring lands.
