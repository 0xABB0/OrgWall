# Coding Guidelines

### MEL-CODE-001: Don't use enums
Enums are, by definition, a closed set.
Whenever you are reaching for an enum, the abstraction is wrong.,
If you find yourself dodging this rule by using constants disguised as enums, it's even worse.
Use enums or enums-adjacent structures only under gabbo's approval.
Tagged unions follow the same idea.

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
