# Rules

Practical rules for writing code in the Melody engine. Philosophy: @vision.md. File layout: @STRUCTURE.md.

Each rule has a unique identifier (e.g. `MEL-001`).

# MEL-STYLE: Code style

## MEL-STYLE-001: Enums are banned

Enums are non-extendable at runtime.

## MEL-STYLE-002: Star stays with the type

`int* x` — the star is part of the type, not the name.

## MEL-STYLE-003: Prefer descriptors over parameter lists

Define an `_Opt` struct + a macro wrapper for designated initializers:

```c
typedef struct {
    void *data;
} Nob_Walk_Dir_Opt;

bool nob_walk_dir_opt(const char *root, Nob_Walk_Func func, Nob_Walk_Dir_Opt);

#define nob_walk_dir(root, func, ...) nob_walk_dir_opt((root), (func), (Nob_Walk_Dir_Opt){__VA_ARGS__})

// call: nob_walk_dir(param1, param2, .data = children);
```

## MEL-STYLE-004: Clangd helper in .inl files

```c
#ifdef _CLANGD
#pragma once
#include "mat4.h"
#endif
```

# MEL: Engine rules

## MEL-001: Memory

All allocations go through `allocator.h` or a specific allocator. No raw `malloc`/`free`. Prefer specific allocators over the generic interface.

## MEL-002: Compiler

Clang only. Heavy use of clang extensions.

## MEL-003: Visibility

Nothing is hidden. Internal functions use `mel__` (double underscore) prefix. No pimpl, no indirection layers, no "c with classes". Be explicit.

## MEL-004: No static-size buffers

No `MAX_*` constants for array sizing. If it's dynamic, make it dynamic — use stretchy buffers.

## MEL-005: No umbrella headers

Headers that only include other headers are not allowed.

## MEL-006: Offensive programming

Assert, don't defend. `assert(ptr)` instead of `if (!ptr) return NULL`.

## MEL-007: Header guards

`#pragma once` only.

## MEL-008: Physical structure

Group by functionality, not type. Full convention in @STRUCTURE.md.

## MEL-009: Language

C only. Exceptions only when forced (Tracy, Imgui, Slang).

## MEL-010: No laziness

Wrong code gets fixed or goes in the todo file. No ignoring problems.

## MEL-011: Coordinate system

World space: Y-up, right-handed. All math, physics, cameras assume Y-up.

Projection matrices are Y-up. The backend adapts to its clip space (Vulkan: negative viewport height). Math layer stays API-agnostic.

2D screen-space (UI, text): `mel_mat4_ortho(0, w, h, 0, ...)` — Y-down, y=0 at top.
2D game-space (platformers): `mel_mat4_ortho(0, w, 0, h, ...)` — Y-up, y=0 at bottom.

## MEL-012: Matrix layout

Row-major. `Mel_Mat4.rows[0]` is the first row. Multiplication order: `projection * view * model`. CPU and GPU use the same layout.

## MEL-013: Handles

Prefer handles over pointers. Use generational handles when needed, plain handles when not.

## MEL-014: Logging

All logging through `mel_log_*` from `log.h`. Never `SDL_Log`, `printf`, or `fprintf(stderr, ...)`.

Levels (lower = more severe):
- `mel_log_fatal` — unrecoverable
- `mel_log_error` — operation failure
- `mel_log_warn` — degraded path
- `mel_log_info` — lifecycle event
- `mel_log_debug` — diagnostic detail
- `mel_log_trace` — verbose

Domain = module name: `"gpu.device"`, `"texture.pool"`, `"font.msdf"`. Demos/examples use app name: `"street-carlos"`, `"tetris"`.
