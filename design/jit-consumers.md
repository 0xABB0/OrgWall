# JIT — consumer behaviors

How the four use cases sit on the four modules. None adds a backend (MEL-ENGINE-IX). REPL and hot-reload/kernels/plugins are patterns over the `repl` loop and the `jit` façade respectively.

## REPL (`repl` module + `clang`'s `Mel_Repl_Lang`)
The loop lives in `repl` (prompt, history, persistent session, dispatch to `Mel_Repl_Lang.eval`). The C language is `mel_clang_repl_lang(jit)`; its `self` holds the `Mel_Jit` plus accumulated top-level declarations as text. Evaluating a line:
1. Classify declaration vs expression vs statement (cheap prefix parse; ambiguous → try expression first).
2. Expression `E` → synthesize `<decls> void __mel_repl_N(void* out){ *(T*)out = (E); }`, `mel_clang_compile` it, `mel_jit_add`, `mel_jit_lookup`, invoke with a result buffer, format by inferred type into `Mel_Repl_Result.text`.
3. Declaration → append to the persistent decls; add a probe unit so errors surface immediately.
Persistent globals stay addressable across lines (every unit's tracker stays live). Non-scalar formatting is best-effort, documented.

A different language = a different `Mel_Repl_Lang`; the loop is untouched.

## Hot-reload (pattern over `jit`)
Map a source file → its `Mel_Jit_Unit`. On change: `mel_clang_compile` (or any frontend) → `mel_jit_replace`. State policy:
- JIT'd **globals** survive by symbol identity only when layout is unchanged; a layout change is a reset, reported.
- For migrated state, the user defines `void __mel_migrate(void* old, void* new)`, called across the swap if present.
- **Live frames are not rewritten** — replace takes effect on next entry; documented, debug-asserted against re-entrant misuse.

## Kernels (pattern over `jit`)
Build a kernel source string at runtime (e.g. an unrolled loop specialized to a known size), compile → IR → `mel_jit_add` → `mel_jit_lookup` the typed function pointer; cache it keyed by hash(source)+opt_level. A hit skips recompilation. Pure composition — no new engine state.

## Plugins (pattern over `jit`)
- Backend created with `expose_process_symbols=false`.
- The host registers an explicit capability table via repeated `mel_jit_define_symbol` — the plugin ABI. Anything off the table fails to link (MEL-CODE-007: no silent ambient access).
- One `Mel_Jit` per plugin (separate backend instance) gives isolation; unload = `mel_jit_destroy`.
- **Not a security sandbox** — JIT'd native code has full in-process memory access; stated plainly (MEL-ENGINE-VIII). Capability bounding is an accident boundary, not a trust boundary.

## Tests (when wired)
- REPL: `1+2*3` → 21; define `int sq(int x){return x*x;}` then `sq(7)` → 49 across lines.
- Reload: v1 returns 1, replace with v2 returning 2, lookup re-resolves to 2.
- Kernel: same source twice → same cached address; different opt_level recompiles.
- Plugin: allow-listed call links; un-listed call fails loud.
