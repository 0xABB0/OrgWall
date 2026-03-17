# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Rules:

- Follow the Guidebook. Treat it as a bible. @GUIDEBOOK.md
- never, ever, write code as not implemented, stubs, hacks to make things work or make crooks. if we cannot implement something because we're lacking some stuff, we implement it.

## Build Commands

The project uses nob. Bootstrap it first:
```bash
cc -o nob nob.c
```

Then use:
```bash
./nob
```

## Testing

Tests live in `tests/` and are compiled into a single binary (`build/tests`). Tests auto-register via `__attribute__((constructor))` — no manual registration needed.

### Writing tests

```c
#include "../melody/test.harness.h"
#include "../melody/whatever.you.need.h"

MEL_TEST(my_test_name, .tags = "mytag")
{
    MEL_ASSERT(true);
    MEL_ASSERT_EQ(1, 1);
}
```

That's it. No `main()`, no `MEL_RUN_TEST()`, no registration boilerplate. The `MEL_TEST` macro generates a constructor that registers the test into a global linked list before `main()` runs.

A test passes if it returns without incrementing the failure counter. A test fails if any `MEL_ASSERT_*` / `MEL_FAIL` fires (which increments the counter and returns early).

### Tags

Tags are optional. A test can have multiple tags separated by commas:

```c
MEL_TEST(my_test, .tags = "allocator, performance")
```

Tests tagged `"visual"` are excluded by default (they need a GPU). Use `--visual` to include them.

### Available assertions

- `MEL_ASSERT(cond)` — generic boolean
- `MEL_ASSERT_EQ(a, b)` / `MEL_ASSERT_NEQ(a, b)` — equality
- `MEL_ASSERT_LT`, `MEL_ASSERT_LE`, `MEL_ASSERT_GT`, `MEL_ASSERT_GE` — comparisons
- `MEL_ASSERT_FLOAT_EQ(a, b, eps)` — float with epsilon
- `MEL_ASSERT_NULL(ptr)` / `MEL_ASSERT_NOT_NULL(ptr)`
- `MEL_ASSERT_STR_EQ(a, b)` — string comparison
- `MEL_FAIL(msg)` — unconditional fail

### Running tests

```bash
./nob test                     # build + run all tests (visual excluded)
./nob test --list              # list all tests with IDs, files, tags
./nob test --filter arena      # run tests with "arena" in the name
./nob test --tag allocator     # run tests matching a tag
./nob test --id 5              # run a single test by ID
./nob test --visual            # include visual tests
```

Args after `test` are forwarded to the test binary. You can also run `./build/tests` directly.

### Existing tags

allocator, collection, math, anim, async, hash, gpu, render, string, ui, event, sim, visual

### Adding a new test file

1. Create `tests/test_yourmodule.c`
2. Write `MEL_TEST(...)` functions with appropriate tags
3. That's it — nob discovers all `.c` files in `tests/` automatically

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

## File phylosophy

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

## Structure

The source code lives in:

- `melody/` — the reusable engine (allocators, math, async, ui, rendering, ecs, etc.)

Outside of that:
- `nob.c` / `nob.h` — build system
- `shaders/` — GPU shader source files
- `assets/` — game assets (images, data files)
- `examples/` — standalone examples showcasing individual engine features (example.{domain}.{name}.c)
- `demos/` — larger demo projects showcasing the full engine working together
- `tests/` — unit tests (test\_{module}.c)
- `design/` — design documents and feature discussions
- `tools/` — standalone utility programs (gen\_test\_texture.c)
- `asm/` — platform-specific assembly for fibers/coroutines (boost.context ports)

Platform-specific code lives in `melody/osx/` (Objective-C .m files).

Inside the melody/ folder, the files are laid out like this:

allocator.arena.* // linear, fixed size allocator (push-only, no Mel\_Alloc wrapping)
allocator.block.* // linear, variable sized allocator with size headers for iteration
allocator.buddy.* // power-of-two splitting allocator
allocator.heap.* // wraps malloc/realloc/free (full Mel\_Alloc wrap)
allocator.leak.* // leak detection decorator (wraps malloc, tracks allocations)
allocator.pool.* // fixed-size block allocator (partial Mel\_Alloc wrap: alloc+dealloc, no realloc)
allocator.ring.* // circular FIFO buffer allocator
allocator.slab.* // multiple pools organized by size class
allocator.stack.* // LIFO allocator with hidden headers
allocator.tracking.* // allocation stats decorator (wraps another Mel\_Alloc)
allocator.vmem.* // virtual memory (reserve/commit/decommit/release/protect)

anim.blend.* // blend/interpolation animations
anim.sprite.* // sprite sheet animations
anim.state.* // animation state machines
anim.timeline.* // animation timeline/keyframe system

async.coro.* // coroutines
async.fiber.* // fibers
async.job.* // async job queue

collection.array.* // dynamic arrays (stretchy buffers)
collection.bitset.* // bit set
collection.btree.* // B-tree
collection.deque.* // double-ended queue
collection.hashmap.* // hash table
collection.heap.* // binary heap
collection.list.* // doubly-linked list (intrusive)
collection.llist.* // singly-linked list
collection.queue.* // FIFO queue
collection.rbtree.* // red-black tree
collection.ring.* // circular ring buffer
collection.set.* // set (backed by hashmap)
collection.skiplist.* // skip list
collection.slotmap.* // slot map with generational handles
collection.sort.* // sorting algorithms
collection.trie.* // trie/prefix tree
collection.compare.* // built-in comparator functions for ordered collections

event.channel.* // decoupled pub/sub channel (fire → callbacks, typed per-channel)

ecs.2d.collider.* // 2D collider component
ecs.2d.collider.editor.* // collider inspector
ecs.2d.sprite.* // 2D sprite component
ecs.2d.sprite.editor.* // sprite inspector
ecs.2d.transform.* // 2D transform component
ecs.2d.transform.editor.* // transform inspector
ecs.world.* // ECS world/entity manager

editor.* // base editor system (imgui)
editor.entities.* // entity list/inspector editor
editor.helpers.* // shared editor utilities
editor.registry.* // editor registration system

font.atlas.* // font texture atlas (module-static pool, typed Mel_Font_Atlas_Handle)
font.desc.* // font descriptor (technique-agnostic font metadata, Mel_Font_Desc_Handle)
font.sdf.* // SDF font rendering (module-static pool, typed Mel_Font_SDF_Handle)
font.msdf.* // MSDF font rendering (typed Mel_Font_MSDF_Handle)

gpu.buffer.* // GPU buffer management
gpu.cmd.* // GPU command buffers (includes mesh shader indirect dispatch)
gpu.descriptor.* // descriptor sets/layouts (descriptor indexing / partially bound support)
gpu.device.* // GPU device abstraction (capabilities: descriptor_indexing, mesh_shader)
gpu.format.* // GPU format helpers
gpu.image.* // GPU image/texture backing
gpu.impl.c // volk implementation (single compilation unit)
gpu.indirect.* // indirect draw buffer helpers (typed VkDrawIndexedIndirectCommand wrapper)
gpu.pipeline.* // graphics pipelines
gpu.scratch_pool.* // transient GPU memory pool (memory-aliased render targets)
gpu.shader.* // shader compilation/reflection (C++ for slang)
gpu.staging.* // per-frame CPU->GPU staging system (batched copy commands)
gpu.storage_pool.* // handle-indexed storage buffer (slotmap + dirty tracking + bulk upload)
gpu.submit.* // command submission
gpu.swapchain.* // swapchain management
gpu.texture.* // texture abstraction layer (format field for UNORM/SRGB selection)

hash.xxh.* // xxHash (XXH3-64)

math.scalar.* // scalar math
math.easing.* // easings (includes X-macro registry MEL_EASING_LIST)
math.rng.* // xorshift64 PRNG
math.vec2/vec3/vec4.* // float vectors
math.ivec2/ivec3/ivec4.* // integer vectors
math.dvec2/dvec3/dvec4.* // double vectors
math.mat3.* // 3d matrix (row-major)
math.mat4.* // 4d matrix (row-major)
math.quat.* // quaternions
math.geo.point2/point3.* // points (aliases to vec types)
math.geo.rect.* // rectangle (float)
math.geo.irect.* // rectangle (integer)
math.geo.plane.* // plane

render.blackboard.* // render data storage (name->value)
render.camera.* // camera (view, projection, position)
render.cull.* // GPU compute culling (frustum cull → visibility bitfield)
render.draw.* // retained draw context (rects, lines → GPU vertex buffers)
render.ecs.delta.* // ECS change detection helpers (added/removed/modified delta lists)
render.graph.* // render graph (data-driven pass execution, owns per-frame resources)
render.list.* // typed render list (retained insert/remove + ephemeral push, sort by key)
render.pass.* // render passes
render.sync.* // ECS→render list sync (flecs observers + bulk update)
render.target.* // render target abstraction (swapchain or offscreen)
render.texture_table.* // bindless descriptor set manager (global texture table)

string.str8.* // utf-8 string (non-owning view)
string.str16.* // utf-16 string (stub)

sprite.batch.* // sprite batching/rendering
sprite.sheet.* // spritesheet data (frames, events, loading)
sprite.sheet.editor.* // spritesheet editor

texture.* // texture loading
texture.atlas.* // texture atlasing
texture.pool.* // texture object pooling

tile.editor.* // tile map/set editor
tile.map.* // tilemap data
tile.set.* // tileset data

ui.layout.* // base layout system
ui.layout.box/grid/advgrid/group.* // layout implementations
ui.native.ctrl.* // native control base (ctrl/event types)
ui.native.event.* // native event definitions
ui.native.{widget}.* // native platform UI widgets (button, label, edit, menu, checkbox, combo, slider, etc.)
ui.widget.* // retained-mode widget base
ui.widget.button/label/panel.* // retained-mode widget implementations

core.types.h // fundamental type aliases (u8, i32, f32, size, etc.)
core.defs.h // utility macros (countof, lengthof)
core.platform.h // platform detection macros
core.engine.h/c // engine struct, init/shutdown, frame loop
core.app.h/c // SDL app entry point (MEL\_APP macro)
core.app.cli.h // CLI entry point (MEL\_CLI macro)
debug.backtrace.h/c // stack trace capture and printing
test.harness.h/c // test framework (auto-registration, unified runner)
gpu.vma.cpp // Vulkan Memory Allocator implementation unit
lib.stb.c // STB image/truetype implementation unit

// todo, find the domain and possibly, splitting in modules for these systems:
- time
- threads
- sound
- debug/logging (profiling, assertions, visualization)
- scripting

## Warning:

This repo is HEAVILY WIP. stuff is not how it should actually be. our job, other than moving this project forward, is also to align this project with changing guidelines.
this CLAUDE.md file, the guidebook and the todo files should give the guidelines.

Since this repo is heavily wip, we have no need to care for backwards compatibility when making changes, and we can make destructive changes

## Good practices

We should strive to keep this CLAUDE.md updated, during discussions we should also strive to stick (and update) the GUIDEBOOK.md.
It's ok to leave holes in the implementation, as long as they are put inside todo.md


## Important instructions

Before designing a new module, you need to negotiate the interface with gabbo.

## Vision for this codebase:

it's EXTREMELY IMPORTANT to follow the vision for this codebase. it takes 100% priority over anything else.

@vision.md

## Design folder

There is a Design folder in the root of the project. this folder is used to discuss features

## Engine growth

The engine grows by implementing stuff and pinpointing the pain points. Everytime we implement something, we shoul thoroughly address pain points (either by fixing them, or putting this in the todo file)
