# Melody Reflect & Meta — Metaprogramming & Runtime-Compilation Substrate

This document specifies the generalization of the enum-to-string codegen (`3f12208`)
into a unified substrate spanning **reflection**, **build-time code generation**, and
**runtime compilation (JIT)**. It is the design that gives a C engine the reach people
seek from Rust's derive/attribute macros, Zig's `comptime` over `@typeInfo`, *and* a live
compiler embedded in the application — full C power, at build time or at runtime, without
the user writing C++.

The document is bound by the Ten Commandments of the Engine. Where a decision turns on
one, the commandment is cited by tag (`MEL-ENGINE-N`).

> **Naming caveat.** Every C identifier, macro, folder, and API name in this document is
> provisional scaffolding chosen to carry the design, not to fix the spelling. The
> symbol-environment presets, the `mel_build_*` entry points, the `meta/` folder, the
> annotation macros — all are placeholders awaiting a deliberate naming pass. Read them as
> roles, not names.

> **Scope caveat.** This revision fuses two concerns that will later be split: the
> *build-model* changes (§4 — modules-as-targets, host-target, the directory convention)
> and the *reflection/compilation* substrate (everything else). They are fused here for
> coherence of the first reading; a later revision separates them, the build-model work
> stated as requirements this substrate imposes on the build system.

---

## 1. The inverting thesis

The prior draft of this design rested on one axiom — *"without a C front-end of our
own"* — and every limitation it confessed flowed from that axiom: no in-place rewriting,
no expression-position macros, comptime quarantined as a deferred "Tier B." This revision
**discards the axiom.** The engine owns a C front-end (`libclang`), a runtime compiler
(`clang` + LLVM), and a loader (`jit`). With those in hand, the boundaries the prior draft
drew in good faith dissolve, and the deferred feature becomes the centerpiece.

The unifying statement:

> **One reflection model. Two generation front-ends. Two evaluation times.** A single
> model of the program's declarations is the input to *both* the declarative path
> (built-in derives, selected by annotation) and the imperative path (user- and
> module-written emitters that walk the same model). The output of either path — C source
> — is realized at *either* of two times: at **build time**, compiled into the target by
> the toolchain; or at **runtime**, compiled in-process by the embedded compiler and bound
> into the live program. The simple path and the powerful path are not merely similar —
> they are byte-identical generator source evaluated at different times (MEL-ENGINE-II).

Against `libclang` the model holds a strictly stronger position than Rust's `syn`: it
carries **resolved semantic types**, sizes, offsets, underlying integer types, and the
spelling the user actually wrote — not raw tokens. A field is known to be a
`Mel_Display_Connector`, both as written and as its canonical `int`, not a sixteen-character
identifier.

---

## 2. Design principles

### P1 — Additive to the user's source, never mutating it

Generators emit *new* translation units; the runtime compiler compiles *new* source. Neither
ever rewrites the user's annotated declaration. This is the one boundary the inverted axiom
keeps: a user who wants a generated body declares the prototype in his header (as
enum-to-string already does) and lets generation supply the definition — the declaration is
real to the LSP, the definition is synthesized. What the inverted axiom *adds* is that
"synthesized" now reaches all the way to runtime: the same prototype may be satisfied by a
build-time `.generated.c` *or* by a function the application JIT-compiles and binds at load.

### P2 — Compose over the same model

Reflection, derives, whole-module emitters, and runtime specialization all read one
`Mel_Refl_Module`. A built-in derive is a generator the engine ships; a comptime emit is a
generator a module or app ships; a runtime specialization is the same generator targeting
the in-process compiler instead of a file. They differ only in authorship and in evaluation
time, never in the model they walk (MEL-ENGINE-IX).

### P3 — The imperative escape is a peer

The declarative derives are conveniences over a full imperative emitter: a host function
handed the whole module that may emit anything. It is not a degraded subset. Test, for any
built-in: *can a user reimplement it as an emit function?* If not, the model is missing a
field — and §5 was widened precisely so the answer is always yes (unions, bitfields,
written-vs-canonical types, the expression behind an array extent are all visible).

### P4 — Provenance or it didn't happen

Every generated construct carries a `#line` directive back to its annotation's source
location. A type error in generated code points at the user's header, not at a generated
line number (MEL-ENGINE-VIII). A derive that cannot honor a request hard-errors naming the
type, the verb, and the location; silent truncation is forbidden.

### Hard prohibitions

- **No hidden cost.** Reflection is opt-in per target. What survives into the shipped binary
  is governed by *per-annotation retention* (§6), not a blanket flag — not one byte of
  metadata reaches the artifact unless an annotation marked `RUNTIME` put it there
  (MEL-ENGINE-III). The embedded compiler/JIT (`compiler`, `jit`) is a separate opt-in whose
  shipped size cost is logged at configure time.
- **No non-hermetic fetch.** Build-time generation runs only in-repo host C. The runtime
  compiler compiles only what the application hands it, never the network.
- **No silent regeneration drift.** Every build-time step is content-gated on its inputs
  *and* the tool that produced it; the model is the single source of truth, so a new
  enumerator or field cannot diverge from its generated table.

---

## 3. Module decomposition

Five modules, each a separately-compiled target (§4), with a strict dependency DAG and a
declared per-platform availability. The decomposition is forced by reality, not taste:
`libclang` (stable C API) does parse/AST/reflection and *cannot emit code*; emitting
executable code needs clang's frontend C++ libraries or a driven toolchain; loading needs a
loader. These are different LLVM surfaces with different stability contracts, so they are
different modules.

**`reflect`** — the IR types (§5) and the query API. Pure data; *zero* clang or LLVM in its
public surface. Depends on nothing. Available on **every platform**: host-compiled it feeds
the build driver, target-compiled it ships exactly the decls that retention marked `RUNTIME`.
This is the spine — everyone touches it, it touches no one — and, per §8, it doubles as the
*runtime ABI description* the `compiler` consumes.

**`clang`** — the `libclang` wrapper: parse C into the `reflect` IR, query the AST. Depends on
`reflect`. Available **on the host always**; **on the target only where `libclang` links and
earns its mass** (desktop, possibly Android) — not iOS, not web. The common runtime path does
*not* carry `libclang`: querying reflection at runtime needs only `reflect` with `RUNTIME`
retention; `clang`-on-target exists solely for the exotic case of parsing *new* C source live
(MEL-ENGINE-I — the door stays open even though the path is rare).

**`codegen`** — the emit machinery, the `EmitSink`, and the built-in derive suite, all over the
`reflect` IR. Depends on `reflect`. *Zero* clang dependency — a derive walks IR and emits
strings; it never sees a `CXCursor`. Available **everywhere**: host-compiled it is the
driver's generators; target-compiled it lets the application run generators at runtime.

**`compiler`** — C source to a loadable artifact. Two backends (§8): a driven toolchain
producing a shared object, and in-process clang + LLVM producing object/IR. Depends on its own
LLVM bits — *not* on the `clang` module, so `reflect`'s stability never chains to the codegen
backend's churn. Heavy; opt-in with a logged size cost. Available on **desktop and Android**;
**present-but-runtime-asserting on iOS** (the JIT entitlement is a publishing constraint, not
a silicon one — §8); **unavailable on web** (no practical LLVM-as-library host).

**`jit`** — load a compiled artifact into the live process, bind symbols against it, hand back
typed function pointers, swap and unload. Depends on `compiler`. Available **as `compiler`**.
Target-axis only: there is no host JIT — the build driver only ever *emits source*, it never
executes generated code in-process.

The dependency DAG: `reflect` is the root; `clang` and `codegen` depend on it; `compiler`
(LLVM) is independent; `jit` depends on `compiler`. The application's cost ladder, each rung
an explicit opt-in (MEL-ENGINE-III): reflection-only, where generators run at build time and
nothing ships → runtime-queryable reflection, where `RUNTIME`-retained decls ship → runtime
code synthesis, adding `codegen` + `compiler` + `jit` → open-world C, the same, with the
application supplying the source.

---

## 4. Build-model requirements

This substrate imposes three demands on the build system. They generalize `prd/10`'s thesis
— *everything is a target* — down to module granularity, which the current single-archive
`modules/build.c` never did.

**R1 — Modules are targets.** Each module is a separately-compiled target with its own
`build.c`, declaring its kind, its dependency edges, and its per-platform / per-runtime
availability. Availability is a property *of the module*, declared once, inherited by every
consumer (MEL-ENGINE-IX). Linking `jit` into a web build is a hard error the consumer need
not foresee — exactly as `prd/04` already mandates for `gui.desktop`. This retires the
backwards model where the umbrella target excludes modules per-platform from the consumer
side. The selection of which `compiler`/`jit` backend is available per `(arch, platform)` is
itself expressible as generated configuration rather than hand-authored branching.

**R2 — Host-target.** A target — or a dependency edge — can be requested *for the host*
regardless of the build's target axes, within one `./nob build <app>`. The reflection driver
is then an ordinary target built for the host, linking the host builds of `clang`, `reflect`,
and `codegen`, produced and cached by the same stage and content-addressed-cache machinery as
everything else. The host driver is not a special tool; it is `codegen` built for the host,
just as runtime reflection is `reflect` built for the target — same modules, same stages, two
axes (MEL-ENGINE-II). This is the first place in the repo where a module builds twice in one
invocation; the build model must carry two simultaneous axis-resolutions.

**R3 — Directory convention.** Modules move from `include/` + `src/` to **`public/`**,
**`private/`**, **`src/`**, **`meta/`**:

- `public/` — the API on the include path for dependents (today's `include/`).
- `private/` — internal headers not exported to dependents.
- `src/` — the program's own implementation, target-axis, gated by the existing
  platform/backend/runtime basename rules.
- `meta/` — **metaprogram code**: derives, comptime emitters, whole-module emitters. It is
  *axis-agnostic*, not host-only. Host-compiled, it *is* the build driver. Target-compiled
  (when the application embeds the `compiler`), it ships so the application runs the *same
  generator source* at runtime against the runtime `reflect` IR. The folder's signal is not
  host-vs-target; it is *metaprogram-over-the-program* versus `src/`'s *the-program-itself* —
  and a single `vertex_layout` derive fires at build time to emit `.generated.c` or at runtime
  to JIT a specialized routine, the application's choice.

---

## 5. The reflection IR

Emitted as C data so that emit functions walk it with plain C and so it can double as runtime
reflection. Shown illustratively as Idris 2 records; the C structs in `reflect/public/model.h`
mirror these. The model carries **everything `libclang` exposes** so that P3's falsification
test holds — a user emit must be able to see what any built-in sees.

Every type reference carries *both* the spelling the user wrote and the canonical resolution,
so a serializer can take the canonical layer while an editor or code generator takes the
written one. Neither is ever discarded.

```idris
data Qual   = QConst | QVolatile | QAtomic | QRestrict
data Extent = Fixed Nat | Flexible | Vla

mutual
  record Sig where
    constructor MkSig
    params   : List TypeRef
    result   : TypeRef
    variadic : Bool

  data TypeExpr
    = Builtin   String
    | Named     String
    | Ptr       (List Qual) TypeRef
    | Arr       TypeRef Extent
    | Func      Sig
    | FnPtr     Sig
    | Qualified (List Qual) TypeExpr
    | UnionRef  String
    | Anon      (List Field)

  record TypeRef where
    constructor MkTypeRef
    written   : TypeExpr
    canonical : TypeExpr

  record Field where
    constructor MkField
    name     : String
    type     : TypeRef
    offset   : Nat
    bitwidth : Maybe Nat
    anns     : List Annotation
    doc      : Maybe String
```

Every integer datum that `libclang` evaluates is captured *with* its source expression, so the
evaluated constant serves layout math while the spelling serves provenance and human-legible
regeneration:

```idris
record ConstExpr where
  constructor MkConst
  value  : Integer
  source : String
```

The declaration records carry layout, location, retained doc comments, and annotations:

```idris
record Enumerator where
  constructor MkEnumerator
  name  : String
  value : ConstExpr
  anns  : List Annotation
  doc   : Maybe String
  loc   : SourceLoc

record EnumDecl where
  constructor MkEnumDecl
  name       : String
  underlying : TypeRef
  signedness : Bool
  consts     : List Enumerator
  anns       : List Annotation
  doc        : Maybe String
  loc        : SourceLoc

record RecordDecl where
  constructor MkRecordDecl
  name     : String
  isUnion  : Bool
  size     : Nat
  align    : Nat
  fields   : List Field
  anns     : List Annotation
  doc      : Maybe String
  loc      : SourceLoc

record TypedefDecl where
  constructor MkTypedefDecl
  name : String
  type : TypeRef
  anns : List Annotation
  doc  : Maybe String
  loc  : SourceLoc

record FuncDecl where
  constructor MkFuncDecl
  name : String
  sig  : Sig
  anns : List Annotation
  doc  : Maybe String
  loc  : SourceLoc

record Module where
  constructor MkModule
  enums    : List EnumDecl
  records  : List RecordDecl
  typedefs : List TypedefDecl
  funcs    : List FuncDecl
```

Function reflection is **signatures, attributes, and annotations only** — never bodies. Bodies
are the province of the `compiler` module, which holds the real AST; carrying them in `reflect`
would bloat the model and tempt the rewriting P1 forbids. `offset`, `size`, `align`, and
`bitwidth` come from `libclang` (`clang_Type_getOffsetOf`, `clang_Type_getSizeOf`,
`clang_Type_getAlignOf`); doc text from `clang_Cursor_getRawCommentText`, retained per §6 like
any annotation.

---

## 6. Annotations as first-class declared entities

`clang`'s `annotate` attribute carries one string, so the string remains the dumb transport
(`mel:serializable(...)`); the *typed model* lives one level up, in a registry the driver
builds by reflecting annotation **declarations**. This keeps the stable `libclang` path and
still delivers a model stronger than Java's or Rust's.

An annotation is a declared entity, the way an `@interface` is in Java. It carries three axes:

```idris
data Retention = Source | Ir | Runtime
data DeclKind  = DKEnum | DKEnumerator | DKRecord | DKField
               | DKFunction | DKParam | DKTypedef

data ArgType = ATInt | ATFloat | ATBool | ATString | ATIdent
             | ATEnum (List String) | ATType | ATAnn String | ATList ArgType

data ArgVal  = AVInt Integer | AVFloat Double | AVBool Bool | AVString String
             | AVIdent String | AVType TypeRef | AVAnn Annotation | AVList (List ArgVal)

record ArgDecl where
  constructor MkArgDecl
  name    : String
  type    : ArgType
  default : Maybe ArgVal

record AnnotationDecl where
  constructor MkAnnDecl
  verb    : String
  retain  : Retention
  targets : List DeclKind
  args    : List ArgDecl

record Annotation where
  constructor MkAnnotation
  verb : String
  args : List (String, ArgVal)
  loc  : SourceLoc
```

**Retention** governs where the metadata survives to, and it *replaces* the prior draft's
coarse `keep_model` boolean entirely — there is no global keep flag, only retention summed
across the annotations actually present (MEL-ENGINE-III, pay for precisely what you marked):

- `Source` — consumed at build time by a derive or emit, then vanishes; no trace in IR or
  binary. `mel:derive(to_string)` is this.
- `Ir` — present in the build-time model so emitters read it, stripped from anything shipped.
- `Runtime` — retained in the shipped runtime reflection table so the application queries it
  live (`@serializable`, `@editor_hidden`, `@net_replicated`). `Runtime` retention is the
  *only* reason a decl's metadata reaches the binary, and it is per-annotation, hence per-decl.

**Target** constrains placement to declared `DeclKind`s; misplacement (e.g. `mel:str` on a
record) is a hard error naming the annotation, the site, and the legal targets (MEL-ENGINE-VIII).

**Argument schema** is the strongest of the three reaches: positional *and* named arguments
with defaults, each typed over the `ArgType` lattice — including `ATType` (resolved against the
live IR, so a `Class`-like argument is a real model node, not an opaque token), `ATAnn` (nested
annotations), and `ATList`. `mel:derive(hash, seed=0x9e37)` is validated against the declared
schema, not parsed hopefully.

Annotations are **module-definable**: a macro declares one, doing double duty — emitting the
use-site attribute macro *and* registering the schema for the driver to enforce. The driver
reflects both the declarations (to build the registry) and the use sites (validated against
it). `reflect/public/enum.h`'s existing macros become a thin special case: `mel:enum_to_string`,
`mel:str`, `mel:skip` are recovered as one declared annotation (`to_string`) with `Source`
retention and enumerator/enum targets.

---

## 7. Generators & metaprograms

A generator is a host (or, at runtime, in-process) function over the model and an emit sink:

```idris
record EmitSink where
  constructor MkSink
  line : String -> IO ()

DeriveGen  : Type
DeriveGen  = (target : Decl) -> Module -> EmitSink -> IO ()

ModuleEmit : Type
ModuleEmit = Module -> EmitSink -> IO ()
```

**Triggering is annotation-driven and global-by-verb.** A module registers `verb -> handler`;
the handler fires wherever *any* reflected decl in the build requests that verb, regardless of
which module or app declared the decl. The legal verbs in a build are the union over all linked
modules' contributions, the engine built-ins, and the app's own. Requesting a verb no linked
module provides is a hard error listing the available verbs (MEL-ENGINE-VIII). Link `gpu`, gain
its `vertex_layout` derive everywhere; do not link it, and the verb is simply absent from the
vocabulary — the capability arrives with the dependency, as a symbol does.

**Visibility splits, and the split is load-bearing.** A `DeriveGen` sees the single decl it is
attached to *plus* the full `Module` for resolution (so it can follow a field's type into
another module's record), but it emits only against its target decl — derives are local and
composable. A `ModuleEmit` sees the entire consuming target's `Module` — powerful, and therefore
scoped to *the target that declares it*.

**The passive/active asymmetry — the rule that keeps linking safe.** A module may contribute
`DeriveGen`s freely: they are passive, doing nothing unless some decl requests their verb. A
module may *not* run a `ModuleEmit` over its consumer: whole-module emitters run **only for the
target that declares them** (the app, or a module over *its own* reflected types). Otherwise
linking a module could rewrite your build's generated surface behind your back — a theft of
cycles you did not request (MEL-ENGINE-III).

Because a `ModuleEmit` receives the whole `Module`, the imperative path covers build-time
constant evaluation directly: a host function computes a table — a sine LUT, a perfect hash, a
parser table — and writes it as C literals, Zig's `inline for (@typeInfo(T))` expressed as a
plain loop over `m.records`.

Shipped built-in derives for the parity-and-suite phases: `to_string` (parity with the existing
tool), `eq`, `hash`, `fields` (a runtime field table), `serialize`/`deserialize` against the
engine's byte sink. Each is a few dozen lines over the model and serves as the worked example
for users writing their own.

---

## 8. Runtime compilation & JIT

This is the feature the prior draft deferred as "Tier B" and the reason the whole stack exists.
Two mechanisms are **peers, not a primary and a fallback** — neither is a degraded subset of the
other:

- **Out-of-process + load.** The `compiler` drives a real toolchain (the host's compiler, or a
  bundled clang) to produce a shared object; `jit` loads it and binds. Ships *no* LLVM; needs a
  toolchain *present*. This serves hot-reload, plugin systems, and any case wanting an actual
  shared library. (TinyCC was considered and rejected: it has no working Mach-O AArch64 backend,
  so on the primary dev target it cannot compile at all — a one-implementation abstraction is
  ceremony, not flexibility, and would violate MEL-ENGINE-IX while pretending to honor it.)
- **In-process LLVM ORC.** The `compiler` embeds clang + LLVM, compiling to IR and JITing
  in-memory with no external toolchain and no temp artifact. Ships tens of MB of LLVM; works
  where no toolchain exists. This serves quick in-process execution — a mod system being the
  archetype. (The engine *exposes* raw-C compilation; whether an application lets a modder touch
  raw C is an application-level convention, never an engine-level amputation — MEL-ENGINE-IV.)

**Platform availability degrades honestly (MEL-ENGINE-VI, VII).** At build time nothing is
gated — generation only touches source and object files. At runtime, calling an unavailable
capability fires a **hard assertion in debug builds** (MEL-ENGINE-VIII); the API surface stays
uniform across platforms rather than being compiled away. iOS is *kept*: a dev-signed,
debugger-attached, or sideloaded build can JIT on-device — the withheld `dynamic-codesigning`
entitlement is a *publishing* constraint, and gating the engine to enforce Apple's store policy
would be the engine imposing its opinion on the developer's distribution choice (MEL-ENGINE-IV,
V, X). Web/wasm is genuinely unavailable — no practical LLVM-as-library host — and says so.

**The graceful-degrade guarantee is the keystone of honesty here.** Anything an application
*would* JIT at runtime from the `reflect` IR — a specialized serializer, a perfect hash, a packed
vertex converter — is mechanically *also* a build-time generator over the same IR, because it is
the same `meta/` source (§4) at a different evaluation time. So on iOS-without-entitlement or on
web, the application gets the **pre-baked** artifact instead of the live one, from identical
generator code. Open-world user-C (modding) is, by physics, simply absent on those targets, and
hard-errors rather than emulating.

### The symbol-binding contract

When runtime-compiled C calls into the live process, and when the host calls into freshly
compiled code, four things are fixed.

**Symbol visibility — one mechanism, two presets.** Compiled code reaches host symbols through an
explicit symbol-environment object with two presets: the *whole-process* environment (resolve
against the process's dynamic symbols) for trusted hot-reload and plugins; and a *curated*
environment (an explicit table of exposed functions and types) for untrusted mods, where reaching
an engine internal must be impossible (MEL-ENGINE-III). The two use cases map one-to-one onto the
two presets, and it is one mechanism, not two code paths (MEL-ENGINE-IX). *(Both preset names in
the eventual API are deliberately left unnamed here — the working tokens were cursed.)*

**The curated surface is reflection-derived — the loop-closing power move.** The curated
environment's typed prototypes are *generated from the `reflect` IR of the engine's own public
API*. The C a mod compiles is `#include`'d against a reflection-generated SDK header and
type-checked against the *real* engine ABI, with symbols bound to live addresses. That is, in
full, "C power without C++": the compiled C is as type-safe against the engine as engine code is,
because reflection produced its headers. This is the single feature that justifies the stack —
and it is why `reflect` is not merely build-time plumbing but a *runtime ABI description* the
`compiler` consumes.

**Host-to-compiled lookup is typed.** Fetching a function out of a compiled module is a typed
lookup against a `reflect` signature, hard-asserting on ABI mismatch in debug (MEL-ENGINE-VIII),
with a raw pointer escape hatch for what the type system cannot name:

```idris
jitLookup : CompiledModule -> (name : String) -> (sig : Sig) -> IO (Maybe AnyPtr)
jitLookupRaw : CompiledModule -> (name : String) -> IO (Maybe AnyPtr)
```

**Reload is a swap; state migration is the application's.** The `jit` module owns a
module-handle abstraction with swap/unload semantics — load-new-then-close-old on the
out-of-process path, ORC resource-tracker remove-then-redefine in-process. Pointers fetched into
old code dangle after a swap and must be re-looked-up; the typed lookup makes that re-bind safe.
Migrating *data* across a reload is explicitly application-level — the engine provides the swap
and the re-bind, never a guess at the developer's domain state (MEL-ENGINE-V).

---

## 9. Build-time pipeline

A target that opts into reflection runs three host stages before its objects are built. The
`clang`/`reflect` modules produce *data*; everything that produces *code* is ordinary host C
compiled into one driver. This is the load-bearing simplification: built-in derives and module
and user comptime are the same kind of thing.

1. **Reflect.** The host build of `clang` parses the target's reflected headers and emits, under
   `build/<variant>/<config>/<target>/generated/`, the IR as C data (`reflect_model.generated.h`
   / `.c`) and a dispatch unit (`reflect_dispatch.generated.c`): extern declarations and a call
   table for every built-in derive the model selects, every linked module's `meta/` contribution,
   and every annotation declaration's schema.

2. **Build driver.** A host target is assembled from the generated model, the generated dispatch,
   the host builds of `codegen` and every linked module's `meta/` sources, and a fixed entry. It
   is a host build (host cc, host arch), independent of the target's axes, produced and cached by
   the ordinary stage and content-addressed-cache machinery (R2).

3. **Run driver.** The driver runs each generator against the model — derives for the decls that
   requested them, then the declaring target's whole-module emitters — concatenating output into
   `reflect.generated.c`. The decls whose annotations carry `Runtime` retention are emitted into
   the shipped model unit; everything else stays in the driver.

`reflect.generated.c` (and the `Runtime`-retained model slice) are appended to the target's
sources, exactly as `run_enum_str_codegen` appends today. Each stage is content-gated on its
inputs, the `clang` tool, and the driver binary. No `dlopen` and no plugin ABI at build time:
module `meta/` contributions are discovered through the dependency DAG and linked statically into
the driver.

---

## 10. Build API surface

`build.h` gains, alongside the retained `mel_build_generate_enum_str` (now a thin alias for
`mel_build_reflect` plus an implicit `to_string` derive, so the display app and `platforms.md`
stay correct). Names provisional:

```
MEL_API void mel_build_reflect_(Mel_Build_Target *t, ...);
#define mel_build_reflect(t, ...) mel_build_reflect_(t, __VA_ARGS__, NULL)

MEL_API void mel_build_embed_compiler(Mel_Build_Target *t, bool enable);
```

`mel_build_reflect` lists header include-spellings to reflect (varargs, NULL-terminated — the
established pattern). A module's `meta/` is auto-discovered through the dependency DAG; no
per-target listing of comptime sources is needed. `mel_build_embed_compiler` opts the target into
shipping the `compiler`/`jit` modules for runtime compilation, logging the size cost at configure
(MEL-ENGINE-III). There is no `keep_model` knob — what ships is decided per-annotation by
retention (§6).

The `runner_codegen.c` machinery — tool bootstrap, `libclang` prefix discovery, host SDK and
builtin-include flags, content gating, generated dir, source injection — is reused; the
per-target invocation grows the host-target driver build/run of §9.

---

## 11. Failure modes (MEL-ENGINE-VIII)

- An annotation use that fails its declared schema (wrong arg type, unknown named arg, missing
  required arg) → hard error at generation, with file:line and the expected schema.
- An annotation placed on a `DeclKind` outside its declared targets → hard error naming the
  annotation, the site, and the legal targets.
- `mel:derive(verb)` for a verb no linked module registers → hard error listing registered verbs.
- A derive applied to a type it cannot serve (e.g. `eq` on a flexible-array record) → hard error
  naming type, verb, location.
- A build-time emitter exits non-zero or crashes → hard error, driver stderr surfaced verbatim.
- Duplicate enum value under `to_string` → warn and skip the alias (existing behavior, preserved).
- `libclang` parse diagnostics at `Error` severity → fatal, as today.
- A runtime call into an unavailable `compiler`/`jit` capability → debug hard-assert naming the
  capability and the platform; in release, a documented failure return, never silent corruption.
- A typed `jitLookup` whose found symbol's ABI contradicts the requested `Sig` → debug
  hard-assert.

---

## 12. Honest boundaries

The inverted axiom (§1) collapses most of the prior draft's confessed limits — the front-end the
old draft said it lacked is exactly what this builds. What genuinely remains:

- **No rewriting of the user's annotated source** (P1). Generation adds new units and compiles new
  source; it never edits the declaration in place. A generated *body* is reached via a declared
  prototype, build-time or runtime.
- **No expression-position hygienic macros.** `macro_rules!` invoked in value position needs
  call-site interception and scope-aware hygiene; even with a front-end, that is a different
  mechanism than declaration- and statement-granularity emission, and is out of scope.
- **Web/wasm has no in-process compiler** — physics, not policy. The graceful-degrade guarantee
  (§8) covers reflection-driven generation there via pre-baking; open-world user-C is absent.
- **Generic bodies are checked per emission.** Monomorphized variants are textual; misuse surfaces
  in generated code, with P4's `#line` pointing home, but not folded into a single integrated
  diagnostic the way an in-language type system would.

---

## 13. Phasing

Phase 0 is a build-system prerequisite (§4); the rest is additive, no API churn across phases.

0. **Build model.** Modules-as-targets to `prd/10`'s "everything is a target"; the host-target
   capability (R2); the `public/private/src/meta` convention (R3); per-module availability and
   dependency propagation (R1). Prerequisite to all reflection work.
1. **Model + parity.** The `reflect` IR at full fidelity (§5); the `clang` module (host); the
   `codegen` built-ins; the host-target driver (§9). Re-express `to_string` as the first built-in
   derive and **prove byte-identical output against today's display app** — the correctness gate.
2. **Annotations + derive suite.** The retention/target/typed-schema system and module-definable
   annotations (§6); `eq`, `hash`, `fields`, `serialize`/`deserialize`.
3. **Extensibility.** Module-contributed metaprograms, the passive-derive / active-whole-module
   asymmetry (§7), `meta/` running at build time; worked examples (LUT, perfect hash).
4. **Runtime.** The `compiler` and `jit` modules (both mechanisms), `Runtime`-retention reflection
   tables, the reflection-derived typed export surface, the hot-reload module-handle (§8).

Hygienic expression-position macros remain out of scope; the front-end-dependent reaches the prior
draft deferred are now *in* scope and distributed across phases 1 and 4.
