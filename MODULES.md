# Modules

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

log.* // logging system (lock-free ring buffer, sink architecture, TLS context)
log.sink.* // log sink interface + built-in sinks (console, file, sqlite, test)

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
- debug (profiling, assertions, visualization)
- scripting
