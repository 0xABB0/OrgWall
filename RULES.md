# Rules

Practical rules for writing code in the Melody engine. For the philosophy behind these, see @COMMANDMENTS.md. For file naming and project layout, see @STRUCTURE.md.

# MEL-META: About this document

## MEL-META-000: Rules are identified by unique identifiers

Each rule in this document is identified by a unique identifier (`MEL-META-000`) as well as a name (`Rules are identified by unique identifiers`).

# MEL-STYLE: Code style

## MEL-STYLE-001: Enums are banned

Enums are non-extendable during runtime. We do not like that.

## MEL-STYLE-002: If a pointer, star stays with the type

In this repo, pointers are considered types. int* x // x's type is "pointer to variable"

## MEL-STYLE-003: Prefer descriptors over parameter lists

We prefer passing descriptors or configurations instead of having a ton of parameters.
In this case, we define the descriptor structure, define the function taking that structure as param with the postfix '_opt' and then define a macro that enables you to call that function like this:

```c
typedef struct {
    void *data;
} Nob_Walk_Dir_Opt;

bool nob_walk_dir_opt(const char *root, Nob_Walk_Func func, Nob_Walk_Dir_Opt);

#define nob_walk_dir(root, func, ...) nob_walk_dir_opt((root), (func), (Nob_Walk_Dir_Opt){__VA_ARGS__})

// then you can call it like this: return nob_walk_dir(param1, param2, .data = children);
```

## MEL-STYLE-004: Clangd helper in .inl files

Inside .inl files, to help clangd, we include the .h file including it inside, and we use this format:
```c
#ifdef _CLANGD
#pragma once
#include "mat4.h"
#endif
```

# MEL: Engine rules

## MEL-001: Memory

Every allocation should go through the allocator interface that is exposed inside allocator.h or directly through a specific allocator.
There should be no usage of raw malloc/free inside the codebase.
We prefer to use the specific allocator instead of the generic allocator interface, but sometimes we are either forced to, or that piece of code is not performance-critical.
If needed, we can make multiple functions that take different allocators each

## MEL-002: Compiler

We only support clang as a compiler, and we want to make heavy use of clang specific extensions

## MEL-003: Visibility

We don't believe in hiding stuff. when things are internal, we prefer to explicitly define functions with a "mel__" (double underscore) prefix (used when it makes sense to expose this kind of functions. example for this is in allocator.arena.h/.inl, we expose the macros to alloc, but for those, we must export also the internal allocation functions)

We don't want to create indirections. they make the code less clear and more error prone. if we can make something explicit, we do.
We dislike pimpl-style, we dislike "c with classes". Even though sometimes we are required to use them, we prefer not to when possible.

## MEL-004: No static-size buffers

When allocating more than one object, we never (unless we have a good reason to) define buffers with a static size.
We never define a MAX_* constant to create an array lazily. if it needs to be dynamic, we make it dynamic. We create a stretchy buffer.

## MEL-005: No umbrella headers

We explicitly don't allow umbrella headers (aka headers that only include other headers)

## MEL-006: Offensive programming

We prefer offensive programming, aka making heavy use of assertions instead of defensive programming (for example checking for null and returning null)

## MEL-007: Header guards

We prefer using #pragma once instead of include guards

## MEL-008: Physical structure

Group by functionality, not type. See @STRUCTURE.md for the full file naming convention and include rules.

## MEL-009: Language

We prefer using only c as our language of choice, though sometimes we are forced to use another language. This should be done sparingly and only when there is no other choice.
Examples: Tracy, Imgui, Slang integration

## MEL-010: No laziness

We do not condone being lazy. If we spot something that's wrong, we either fix it, or we write in the todo file what the problem is

## MEL-011: Coordinate system

The engine uses a Y-up, right-handed coordinate system for world space. This is the engine's authoritative convention — all math, physics, cameras, and scene graphs assume Y-up.

Each graphics API has its own clip space conventions:
- Vulkan: Y-down, Z [0,1]
- DirectX: Y-up, Z [0,1]
- Metal: Y-up, Z [0,1]
- WebGPU: Y-up, Z [0,1]

Projection matrices (`mel_mat4_perspective`, `mel_mat4_ortho`) are written in Y-up convention. The API backend is responsible for adapting to its native clip space at the viewport level. For Vulkan, this means negative viewport height (`VkViewport { .y = height, .height = -height }`). Other backends do nothing. This keeps the math layer API-agnostic — projection matrices never contain API-specific corrections.

For 2D screen-space rendering (UI, text, overlays), use `mel_mat4_ortho(0, width, height, 0, ...)` — this maps y=0 to the top of the screen (Y-down screen space). For 2D game-space rendering (platformers, physics), use `mel_mat4_ortho(0, width, 0, height, ...)` — this maps y=0 to the bottom (Y-up, consistent with world space).

## MEL-012: Matrix layout

Matrices are row-major. `Mel_Mat4.rows[0]` is the first row. Matrix multiplication order is `projection * view * model`. This applies to both CPU math and GPU shader data — push constants and buffers send row-major data, shaders consume it as-is via `mul(matrix, vector)`.

## MEL-013: Handles

Make heavy use of handles. we should limit the number of pointers in this engine. sometimes it makes more sense to use a generic handle and not a generational handle.
