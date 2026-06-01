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
This repo has a .clang-format; use that and format often

### MEL-CODE-005: Pillars of code
Our code should follow the following pillars:
- Code must be correct.
- Code must be idiomatic
- Code must be fast
- Code must be extendable

### MEL-CODE-006: Make heavy use of profiling and logging
Always push for useful logging but try not to make too much noise.
Profiling is extremely important, both speed and memory.

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

