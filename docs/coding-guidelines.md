# Coding Guidelines

### MEL-CODE-001: Don't use enums
Enums are, by definition, a closed set.
Whenever you are reaching for an enum, the abstraction is wrong.,
If you find yourself dodging this rule by using constants disguised as enums, it's even worse.
Use enums or enums-adjacent structures only under gabbo's approval.
Tagged unions follow the same idea.
The correct use of enums is when the answer to the following questions is yes: "Are we implementing a protocol? Is this code never going to be touched again unless the protocol changes (and then we'd have to change the code anyway)?"

### MEL-CODE-002: Never use fixed-size arrays
Every time you find yourself writing an array of size [MEL_MAX_*], you are wrong.
Fixed arrays cannot at all be expanded and create the worst failing cases.
Use dynamic arrays instead

### MEL-CODE-003: Use Allocators correctly
Anytime that a function needs to use memory, it should take an allocator either through a parameter, or the parameter(s) should contain an allocator.
When you need to give some code an allocator, NEVER use mel_malloc(). it completely defeats the purpose of having allocators.

### MEL-CODE-004: Follow formatting conventions
This repo has a .clang-format; follow formatting

### MEL-CODE-005: Pillars of code
Our code should follow the following pillars:
- Code must be correct.
- Code must be idiomatic
- Code must be fast
- Code must be extendable

### MEL-CODE-006: Make heavy use of profiling and logging
Always push for useful logging but try not to make too much noise.
Profiling is extremely important, both speed and memory.

### MEL-CODE-007: Silent defaults should be avoided
Unless explicitly instructed, avoid at all costs having defaults.
Silent defaults make debugging much, much harder.

### MEL-CODE-008: C++ is the core, C is a projection on top (inverted hourglass)
Modules are C++-first. The inter-module contract is C++: headers carry types, templates, RAII.
When a module needs a C-callable surface, it ships a separate `<module>_c` target that projects the C++ core down to a C ABI.
C is never the cross-module spine; it is a crafted veneer over the C++ core.
Rationale: the engine builds from source under one hermetic toolchain per platform, so C++'s lack of a stable cross-compiler ABI — the reason the classic hourglass narrows to C at the waist — does not apply inside the graph. Within the build, C++ crosses module boundaries freely.

### MEL-CODE-009: One floor standard governs every exported header
A single repo-wide C++ floor standard governs all of `include/`.
Any C++ entity that crosses a module boundary (type, inline function, template) is compiled in the consumer's translation unit under the consumer's standard; therefore exported headers must be ODR-identical under every consumer and may use floor features only.
A module may compile its own `src/` at a higher standard, but nothing higher may leak into `include/`.

### MEL-CODE-010: `include/` is the contract, `src/` is free
The directory boundary is the rule. Editing `include/<m>/` changes the ABI/ODR contract and is handled with care; editing `src/` cannot break a consumer.
Exported headers are ODR-frozen: no `#if __cplusplus` that changes shape, no layout-affecting standard-version attributes (e.g. `[[no_unique_address]]`) on exported types, no exported template reaching past floor features.

### MEL-CODE-011: The `<module>_c` veneer has one rigid shape
Opaque forward-declared handle, explicit create/destroy (RAII does not cross the boundary), `extern "C"` linkage, errors returned through an out-parameter, never an escaping exception.
The shape is mechanical by design: trivial to fill correctly, trivial to generate.

### MEL-CODE-012: The C veneer must be zero-cost or coarse
A `<module>_c` call is one non-inlined crossing of the library seam unless LTO bridges it.
Either enable ThinLTO for the interop, or design the C surface coarse-grained (batch operations, not per-element) so the crossing amortizes.
"Performance-equal to C" is a requirement, not a hope — make it a build fact.

### MEL-CODE-013: Enforce the floor with a canary, not a bespoke lint
A single test target compiles every `include/**` at the floor standard and fails if it needs more.
That is the entire enforcement surface for MEL-CODE-009 and MEL-CODE-010 — no custom build aspect.

# Spec Guidelines

### MEL-SPEC-001: Specifications are not a novel
Specification should be straight to the point.
Avoid verbose prose as much as possible.
Focus instead on delivering meaning.

### MEL-SPEC-002: Spec folder should be tidy
Specifications in design/ should be removed as soon as there is place for them in the relative module

### MEL-SPEC-003: Temporal information
Strictly avoid temporal information in the specification.

### MEL-SPEC-004: Keep a sane context
Future readers will not have the conversation context; never assume knowledge of information not present in the repo (eg: chat history, deleted specifications)

