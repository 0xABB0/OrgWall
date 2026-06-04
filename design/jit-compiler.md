# JIT compiler integration — master design

Runtime compilation and evaluation of code, decomposed into **four orthogonal modules** along two pluggability axes: the *language frontend* (what `repl` abstracts) and the *execution backend* (what `jit` abstracts). C-via-Clang over LLVM-ORC is the first concrete path; a custom language and/or a custom backend plug into the same interfaces (MEL-ENGINE-IX).

## Locked decisions
- Source language (first frontend): **C**, via Clang.
- Execution backend (first): **LLVM ORCv2 LLJIT**.
- **LLVM IR is the lingua franca** at the frontend→backend boundary. The unit crossing is an in-memory LLVM IR module (`Mel_Jit_Module`, an opaque handle the `jit` interface forward-declares and `llvm` defines). Any IR-producing frontend therefore works with any IR-consuming backend.
- **C-as-a-REPL-language glue lives in `clang`** — importing `clang` yields a ready `Mel_Repl_Lang`.
- Platforms: native hosts incl. iOS (App-Store JIT restriction is the app dev's concern). `wasm` out of scope (ORC cannot codegen in-browser).
- No enums (MEL-CODE-001): backend/frontend selection is "hand me the vtable," never a kind-tag.

## The four modules
```
core / allocator / string / log         (existing base)
        │
   ┌────┴─────┐
  jit        repl                        pure C interfaces (vtables), no LLVM
   │           │
  llvm ────────┤                         C++ shim: IR build/parse + ORC backend (impl of jit)
   │           │
  clang ───────┘                         C++ shim: C→IR frontend + C Mel_Repl_Lang binding
```
- **`jit`** (pure C). Defines `Mel_Jit_Module` (opaque IR handle), `Mel_Jit_Unit` (generational handle), `Mel_Jit_Result{ok,diagnostics}`, and `Mel_Jit_Backend` (a vtable: add/replace/remove/lookup/define_symbol/destroy). A `Mel_Jit` façade holds a backend and dispatches. Knows nothing of LLVM. Deps: core/allocator/string.
- **`llvm`** (C++ shim over the vendored toolchain). Produces IR — `mel_llvm_parse_ir(text)` now, an IR-builder surface later (the path for *your own language*) — and supplies `mel_llvm_orc_backend()`, the ORC implementation of `Mel_Jit_Backend`. Defines `struct Mel_Jit_Module` (wraps an `LLVMOrcThreadSafeModule`). Deps: `jit`, `llvm-runtime`, base.
- **`clang`** (C++ shim over the vendored toolchain). `mel_clang_compile(src)` → `Mel_Jit_Module*` (C → LLVM IR, via `llvm`), and `mel_clang_repl_lang(jit)` → a `Mel_Repl_Lang` that compiles+adds+looks-up+invokes C lines. Deps: `llvm`, `jit`, `repl`, `llvm-runtime`, base.
- **`repl`** (pure C). Defines `Mel_Repl_Lang` (vtable: eval a source line in a session → printable `Mel_Repl_Result`) and the loop (prompt, history, persistent session). Backend/language agnostic. Deps: core/allocator/string.

Interface signatures: `design/jit-interfaces.md`. Vendoring: `design/jit-vendor-llvm.md`. Consumer behaviors (REPL loop, hot-reload, kernels, plugins): `design/jit-consumers.md`.

## Why this split (against Gabbo's stated intents)
- `llvm` apart from `clang` → build a custom language: emit LLVM IR, `mel_llvm_parse_ir` it, run it on any backend, no C frontend involved.
- `jit` apart from `llvm` → swap the execution backend (ORC ↔ MCJIT ↔ a custom IR-JIT ↔ an IR interpreter). All consume the same `Mel_Jit_Module`.
- `repl` apart from everything → plug different languages into one loop; C is merely the first `Mel_Repl_Lang`.

## Platform / capability matrix
- `macos`/`linux`/`win32`: full native JIT; prebuilt LLVM+Clang per host (one tarball backs both `llvm` and `clang`).
- Apple arm64: ORC needs `MAP_JIT` + the `com.apple.security.cs.allow-jit` entitlement; backend creation fails loud (naming it) if absent.
- `ios`/`android`: `mel_unavailable` until a cross-built toolchain artifact exists (MEL-ENGINE-VIII — no broken shadow).
- `wasm`: `mel_unavailable`. Honest, not silent.

## Failure modes (iterated)
- **Compile error** → `Mel_Jit_Result{ok=false, diagnostics}`; no partial module added; session intact (REPL keeps going).
- **Frontend/backend artifact mismatch** — impossible by construction: the currency is standardized as LLVM IR; any frontend's `Mel_Jit_Module` is consumable by any backend.
- **Undefined extern with `expose_process_symbols=false`** → link diagnostic, never a silent null call (MEL-CODE-007).
- **Symbol redefinition across units** → tracker ordering decides; a duplicate strong symbol is reported, not silently shadowed.
- **W^X/entitlement absent on Apple arm64** → loud failure at backend create.
- **Header/lib version skew** → pinned single toolchain version; mismatch is a build-time failure.
- **Allocator boundary** — engine-owned memory (façade, handle tables, diagnostics) flows through `Mel_Alloc`; LLVM/Clang allocate internally via C++ `new`; that boundary is documented, not laundered (MEL-ENGINE-VIII). `*_string_free` frees diagnostics through the owning allocator.
- **Hot-reload over live frames** — replace takes effect on next entry; live frames are not rewritten; documented + debug-asserted against re-entrant misuse.
- **REPL value of a host-only type** — `Mel_Repl_Lang` returns a best-effort printable form; non-scalar formatting is documented as such.

## Open questions
- IR-builder surface in `llvm` (for custom languages) vs requiring IR text — skeleton ships `parse_ir`; the builder is a follow-on.
- One `Mel_Jit` per plugin (isolation) vs a shared `ExecutionSession` — spec assumes isolation.
- `opt_level` as a single `u32` vs a richer pass-pipeline knob — spec assumes the former.

## Revisions (implemented + verified)
- **Toolchain: all-dynamic, first-class.** One shared `libLLVM` (macOS: Homebrew) linked by both `llvm` (ORC) and `clang` (incl. `clang::Interpreter`) → a single LLVM image + `TargetRegistry`. Static linking crashed (libc++21 TMO); static-LLVM + embedded-LLVM-in-libclang-cpp gave two registries (Interpreter couldn't find a target). See `jit-vendor-llvm.md`.
- **REPL backend split.** The C REPL is `clang::Interpreter` (owns its ORC JIT) — incremental, typed values, persistent globals, native classifier. It does **not** use the pluggable `jit` backend; `jit`/`llvm` still serve `mel_clang_compile` (kernels/plugins) and custom frontends/backends. `mel_clang_repl_lang(a)` no longer takes a `Mel_Jit`.
- **Verified:** jit-facade 2/2, repl-loop 7/7, llvm-orc 6/6 (incl. process-symbol isolation, replace-keeps-handle, define_symbol), clang-frontend 4/4 (typed values, persistent globals); `apps/repl/repl-cli` runs an end-to-end C REPL.
- **Open:** Linux/Windows dynamic-LLVM wiring + path discovery (Windows `clang::Interpreter` unproven); `mel_jit_lookup` out-diagnostic (M-5); drop the now-unnecessary IR-text roundtrip in `mel_clang_compile`.

Per MEL-SPEC-002, these files are deleted as their content lands in the four modules.
