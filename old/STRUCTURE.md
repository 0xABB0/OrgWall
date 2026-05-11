# Structure

## Types

Strings: `str8` (UTF-8), `str16` (UTF-16) — `{ u8* data; size len; }` where `size = ptrdiff_t`.

`S8("literal")` for string literals. `str8_from_cstr()` at third-party boundaries.

Sized aliases: `u8 u16 u32 u64`, `i8 i16 i32 i64`, `f32 f64`, `size` (ptrdiff_t), `usize` (size_t).

## File naming

Pattern: `domain.module.suffix`

- `domain` = role in the engine (allocator, gpu, render, ...)
- `module` = the thing itself (arena, device, graph, ...)

Full file family for a module (e.g. `allocator.arena`):

```
domain.smd                    // feature specification (smd = spec markdown)
domain.module.amd             // architecture specification (amd = architecture markdown)
domain.module.h               // main interface
domain.module.fwd.h           // forward declarations
domain.module.cfg.h           // configuration macros (ifdef defaults)
domain.module.inl             // inline implementations (included by .h)
domain.module.c               // main implementation
domain.module.xxx.c           // split implementations / platform variants
domain.module.md              // module documentation
domain.module.test.spec.c     // tests (nob filters out of libmelody.a)
domain.module.bench.spec.c    // benchmarks (nob filters out of libmelody.a)
```

## Include rules

- In `.h` files: include `.fwd.h`, not the full header.
  - Exception: struct embedded by value requires the full header.
- In `.c` files: include full headers.
- `core.types.h` and `core.defs.h` can be included freely from anywhere.
- No umbrella headers (see MEL-005).

## Configuration files (.cfg.h)

Every macro is always defined with a default. User can override before include.

```c
#ifndef MEL_ALLOC_RECORD_STACKTRACE
#if MEL_ALLOC_DEBUG_LEVEL == 4
#define MEL_ALLOC_RECORD_STACKTRACE 1
#else
#define MEL_ALLOC_RECORD_STACKTRACE 0
#endif
#endif
```

## Directory layout

- `melody/` — engine source
- `melody/osx/` — platform-specific (Objective-C)
- `examples/` — single-file examples (`example.{name}.c`)
- `demos/` — multi-file demo projects
- `tests/` — legacy tests (new tests go in `melody/` as `domain.module.test.spec.c`)
- `shaders/` — GPU shaders
- `assets/` — game assets
- `design/` — design documents
- `tools/` — standalone utilities
- `asm/` — platform assembly (fibers/coroutines)
- `nob.c` / `nob.h` — build system
