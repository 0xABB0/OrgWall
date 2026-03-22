# Structure

## Types

strings -> str8 (UTF-8) or str16 (UTF-16)
- u8* data
- size len (size = ptrdiff_t, signed)

Use S8("literal") for string literals, str8_from_cstr() at third-party boundaries.

the usual sized aliases:
u8 u16 u32 u64
i8 i16 i32 i64
f32 f64
size (ptrdiff_t)
usize (size_t)

## File philosophy

every module can have multiple files. the file naming strategy is:

domain.module.suffix

domain is the role of the module in the bigger picture.
module is the modules name.
suffix is the file extension and something more.

example: a linear, fixed size allocator (arena)

domain = allocator
module = arena

files:

allocator.arena.h // main interface
allocator.arena.cfg.h // configurations for that specific module. usually contains ifdefs and stuff like that.
allocator.arena.fwd.h // forward declaration
allocator.arena.inl // inline definitions (still declared inside the .h. this file is still included by the .h)
allocator.arena.c // main (not inlined) implementation
allocator.arena.xxx.c // if there is a need to split the file into multiple implementations
allocator.arena.md // module documentation (design notes, API contracts, usage)
allocator.arena.test.basic.c // tests (filtered by nob, not compiled into libmelody.a)
allocator.arena.bench.throughput.c // benchmarks (filtered by nob, not compiled into libmelody.a)

the general rule for inclusion is:
never include the main interface from an interface file. if you are in a .h, include the .fwd.h file.
exception: when a struct embeds another struct by value (not pointer), you must include the full header to get the struct definition. this is acceptable and expected (e.g. render.target.h includes gpu.image.h for Mel\_Gpu\_Image by value, render.list.h includes gpu.buffer.h for Mel\_Gpu\_Buffer by value).
inside implementation files, you should only include interface files

there are some files (eg types.h, defs.h) that do not follow this. you can include those files freely

the configuration file is used to define macros that are needed by the module.
every macro will be always defined and, usually, has a default logic to pick it up, unless it's explicitly defined.
example: check if we want to trace every stacktrace for every allocation:
ifndef MEL\_ALLOC\_RECORD\_ALLOCATIONS\_STACKTRACE // if defined skip
if MEL\_ALLOC\_DEBUG\_LEVEL == 4
define MEL\_ALLOC\_RECORD\_ALLOCATIONS\_STACKTRACE 1
else define as 0
endif
this way, we can easily toggle a specific configuration if needed, and give good defaults

## Directory layout

The source code lives in:

- `melody/` — the reusable engine (allocators, math, async, ui, rendering, ecs, etc.)

Outside of that:
- `nob.c` / `nob.h` — build system
- `shaders/` — GPU shader source files
- `assets/` — game assets (images, data files)
- `examples/` — standalone examples showcasing individual engine features (example.{domain}.{name}.c)
- `demos/` — larger demo projects showcasing the full engine working together
- `tests/` — legacy unit tests (test\_{module}.c) — new tests go in `melody/` as `domain.module.test.spec.c`
- `design/` — design documents and feature discussions
- `tools/` — standalone utility programs (gen\_test\_texture.c)
- `asm/` — platform-specific assembly for fibers/coroutines (boost.context ports)

Platform-specific code lives in `melody/osx/` (Objective-C .m files).
