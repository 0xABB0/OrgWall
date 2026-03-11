--- THE EVER GROWING LIST OF THINGS TO DO ---

## Gabbo's notes (sacred and untouchable - any ai should never add to this list)
- [CORE] implement stacktrace capturing for every allocation (debug mode) — DISCUSS: storage format (inline in tracking header vs linked list), debug-only toggling, interaction with Tracy memory profiling. Infrastructure ready (mel\_backtrace\_capture exists).
- [CORE] handle init failure cleanup — PARTIALLY DONE: gpu.device, gpu.swapchain return bool, engine init has goto-based cleanup. Remaining: imgui init failure doesn't fully unwind (skips the feature flag)
- [CORE] ~~implement Virtual File System (VFS) with mount points and archive support~~ PARTIAL: async-first VFS + mounts landed; archive backend still deferred
- [CORE] implement Hot-Reloading for Game Code (.dylib/.dll reloading)
- [CORE] implement Asset Hot-Reloading (file watcher)
- [CORE] implement centralized engine logging system (mel\_log\_*) to replace raw SDL_Log/fprintf
- [CORE] implement Windows crash handling via SEH and StackWalk64
- [CORE] improve backtrace resolution using dladdr() and -rdynamic on Unix-like systems
- [CORE] implement CVar registry (console variables for runtime tuning) - new module
- [CORE] melody should handle much more initialization autonomously (vulkan, editors, etc). The app should dictate what it wants from melody via MEL\_APP params or similar.
- [SYSTEM] implement basic audio system (audio.h/c)
- [SYSTEM] ~~implement distributed event system (specialized queues, no global bus)~~ PARTIAL: event.channel primitive done (typed pub/sub, sync callbacks, decoupled wiring). Remaining: integrate into engine systems (SDL event conversion, ECS hooks, editor), build dispatcher layer if needed for many-to-many routing
- [SYSTEM] implement input action mapper (input.h)
- [SYSTEM] implement serialization/config system
- [RENDER] ~~implement render graph (data-driven passes)~~ PARTIAL: render.graph.h/c implemented with DAG compilation, barrier computation, per-frame resource ownership, parameterless execute, production hooks. Remaining: memory aliasing for transient targets, GPU counter reset for write lists
- [RENDER] support multiple windows/viewports/cameras
- [RENDERING] the engine should provide built-in rendering (sprites, etc) that works out of the box via the frame graph, without requiring ECS. App can add/remove passes.
- [ARCH] implement view/input router (View struct, window_id, layer)
- [EDITOR] implement reflection system (Type Descriptors) for generic inspection
- [ECS] replace hardcoded entity factories with prefab/blueprint system
- [ECS] spatial partition for physics (grid/quadtree)
- [TEST] expand test coverage to include ECS, Asset Registry, and VFS
- [TEST] implement fuzz testing for custom allocators (Arena, Block, Buddy, etc.)
- [TEST] strengthen tests overall — better test harness that could be used by the game itself (and even mods?)
- [BUILD] use pkg-config or config file to remove hardcoded paths in nob.c
- [DEMO] make a demo with a ton of animated sprites (same atlas, different frame, different parameters - size, direction, tint)
- [ENGINE] the engine should expose an "editor" system as entrypoint for every editor to register into. Multiple editors open at any time (even multiple instances of the same editor, pointing to different things).
- [ENGINE] make melody run "headless" (two modes: no graphics API at all, or no windows). Needed for testing vulkan (output to png?). Could also be "run for x frames".
- [ENGINE] unify window creation — window.h API exists but 13+ examples still call SDL_CreateWindow directly, and ui.native.window.* is a separate path. All window creation should go through mel_window_create
- [ENGINE] variable rate updates and fixed updates at the same time (discuss: multiple fixed steps?)
- [ALLOCATOR] growable allocators (block, heap, others?) should all be backed by vmem
- [PROFILING] Tracy coverage expanded (allocator.tracking, gpu.submit, gpu.buffer, texture, render.graph). Still need: more coverage across the codebase, and profiling graphics API calls
- [PROFILING] debug ui (imgui) with overview of total memory, ecs debugging, etc.
- [UPSTREAM] imgui SDL3 backend loses mouse capture during viewport drag/resize (ocornut/imgui#8591, #8869) — known upstream bug, track and update local copy when patched.
- [PHYSICAL STRUCTURE] higher-level files (eg ui.widget.[c|h|.fwd.h]) should sort alphabetically above sub-modules (eg ui.widget.button.\*). Currently *.button comes before *.c
- [DISCUSS] is there a way to test for bad behaviour in memory access?
- [DEMO] we need a demo for widget animations
- [DEMO] we need a demo for spritesheet animations
- [DEMO] we need a demo for cutscenes
- [DEMO] we need a demo for sprites
- [STRING] Porter Stemmer algorithm
- [STRING] String builder
- [ENGINE] ~~migrate legacy `Mel_Engine` + `SDL_Window` coupling to module statics + per-window `Mel_Window` ownership~~ DONE: `Mel_Engine` struct removed, module statics with accessors (`mel_gpu_dev()`, `mel_allocator()`, etc.), window/swapchain registries in place
- [EXAMPLE] we need to make a boomer shooter example
- [EXAMPLE] we need to make a 3d example
- [EXAMPLE] we need an example that merges 2d with 3d
- [DEMO] text animations
- [FIX][EXAMPLE] example.trie is broken. it flickers
- [FIX][EXAMPLE] example.pathfind is broken. when it renders without walls, there are a ton of artefacts, and the grid breaks.
- [BUILD] the build system we're using right now (nob.h) is fine, but at this point i think that melody can include that in its own source code i think
- [ALLOC] we should remove the mel\_alloc\_heap() function. everyone that needs to allocate needs to get passed an allocator directly.
- [BUILD] we need to have a way to optimize the release for a certain system/architecture/cpu (march flag, using handcrafted assembly per-feature, whatever)
- [BUILD] we need a better method to define if a source file is platform specific (build system-wise). i don't like having a map of "for this, skip these", maybe it could be made less ugly by doing something like ".osx -> defined(__APPLE__)"
- [CAMERA] Consider if it's worth having a Camera2D in addition to the Camera we have now, to have an extremly optimized 2d pipeline if needed
- Carlos demo still has a ton of friction i don't like:
-- It defines its own pass for resolution independance. this should be a game engine's feature.
-- It defines a pass for imgui. this should be given for free by the engine
-- 

## Engine refactor gaps (Mar 2026)

- [ENGINE] `mel_process_event()` only forwards to imgui — no longer handles `SDL_EVENT_WINDOW_RESIZED`. Swapchain registry has `resize_requested` field on entries but nobody sets or reads it. Need to wire resize events through the swapchain registry.
- [ENGINE] sprite pass hardcoded to `VK_FORMAT_B8G8R8A8_SRGB` in `mel_init_opt` (core.engine.c:141). Previously used the swapchain's actual format. Could mismatch if swapchain picks a different format.
- [ENGINE] `mel__engine_init()` / `mel__engine_shutdown()` are vestigial — init just calls `mel_backtrace_init()`, shutdown is empty. Should fold into `mel_init()` / `mel_shutdown()` or become constructors.
- [ENGINE] `mel_engine_shutdown` still has defensive null-check (`if (!engine->window) return`) instead of asserting — violates MEL-X-006 (from old audit, verify if still applies after refactor)

## Audit findings (Mar 2026)

- [ENGINE] ~~core.engine.h/.c are out of sync~~ RESOLVED: `Mel_Engine` struct removed entirely, replaced with module statics
- [GPU] MEL_MAX_FRAMES_IN_FLIGHT defined independently in render.draw.h, render.graph.h, sprite.pass.h (all as 3 with #ifndef guards). Should live in a single cfg file (gpu.cfg.h or render.cfg.h)
- [ASYNC] async.io.h handlers[MEL_IO_MAX_HANDLERS] is a fixed-size array for a dynamic operation (handler registration). Violates MEL-X-004. Should be a stretchy buffer
- [ALLOC] raw free() calls in tile.set.c:326, tile.map.c:331 (cJSON output), test.visual.c:33/92/222/249 (bare malloc+free for pixel buffers), debug.backtrace.c:125 (backtrace_symbols). Violates MEL-X-001
- [GPU] gpu.device.h is a transitive include monster — pulls in SDL, vulkan.h, volk.h, vk_mem_alloc.h. 7 gpu.*.h files include it just for Vulkan types (VkBuffer, VmaAllocation, etc.), not for Mel_Gpu_Device. Needs a lightweight gpu.vulkan.types.h or similar that provides VK/VMA type definitions without the device struct. Also includes full allocator.h when only allocator.fwd.h is needed (struct has pointer only)
- [EDITOR] editor.h has fixed-size char buffers (pending_file_path[512], texture_picker_filter[128]) — violates MEL-X-004. Also still uses raw SDL_Window* instead of Mel_Window_Handle
- [GPU] gpu.device.c uses hardcoded stack arrays: extensions[32] (line 93) and enabled_exts[8] (line 320). If extension count exceeds these, silent overflow. Should alloc based on actual count
- [STYLE] ecs.2d.transform.editor.h uses #ifndef include guard instead of #pragma once — only header in the codebase not using pragma once (MEL-X-007)
- [STYLE] inconsistent cleanup function naming: _shutdown (gpu, render, vfs), _destroy (ui.widget, window, anim), _free (collections, allocators). No documented rule for which to use. Violates MEL-COMMAND-IX
- [ENGINE] mel_engine_shutdown has defensive null-check (`if (!engine->window) return`) instead of asserting — violates MEL-X-006 (offensive programming)
- [ARCH] window.c and swapchain.c maintain unsynchronized global Mel_SlotMap statics with no thread-safety documentation on their public APIs. Per design checklist, every public function needs a thread-safety statement

## Friction from demo.breakout / demo.pathfind / demo.anim (Feb 2025)

- [ASYNC] no way to cancel/abort a running coroutine externally. Only option is `mel_coro_destroy` on the entire context. Need `mel_coro_cancel` that returns a coroutine to the free list. → update: async.coro.h/.c
- [GPU] texture struct doesn't expose width/height at the top level — must dig into `Mel_Gpu_Image` internals. → update: gpu.texture.h
- [RENDERING] coordinate system is confusing: ortho(0, w, 0, h) suggests Y-up but sprites render Y-down. Worth documenting or standardizing. → update: docs or sprite.pass
- [RENDERING] no built-in letterboxing/scaling for fixed-resolution games. Ortho projection uses swapchain extent which may differ from design resolution. → update: engine.h or new render utility

## Friction from demo.easing / demo.rbtree / demo.skiplist / demo.trie (Feb 2025)

- [TRIE] no "collect all words with prefix" API — `mel_trie_starts_with_str` returns bool only. demo.trie.c had to manually walk `Mel_TrieNode` internals (children, child_keys, child_count, has_value) via custom DFS (~50 lines). Need `mel_trie_collect_prefix_str` or similar. → update: collection.trie.h/.c
- [COLLECTION] `void*` key pattern requires verbose `(void*)(intptr_t)value` casts on every insert/find/remove. Consider convenience macros like `mel_rbtree_insert_int` for common integer key case. → update: collection.rbtree.h, collection.skiplist.h, collection.trie.h
- [INPUT] SDL3 scancode ordering gotcha: SDL_SCANCODE_0 (39) comes AFTER SDL_SCANCODE_9 (38), making naive `>= _0 && <= _9` range checks always false (caught by -Wtautological-overlap-compare). Same for keypad. Consider `mel_scancode_to_digit` utility. → update: new input utility or docs


## Friction from render.draw implementation (Mar 2026)

- [RENDERING] Mel_Draw_Vertex duplicates Mel_SpriteVertex binary layout — intentional (shares sprite pipeline/shader). When the material system ships, draw ctx gets its own pipeline and the vertex type diverges. Known coupling. Note: Mel_SpriteVertex is now file-local in sprite.pass.c
- [RENDERING] render.draw takes pipeline/texture/dev externally — partially addressed: demos now use `engine->sprite_pass->pipeline` and `engine->sprite_pass->white_texture` instead of local copies. Still requires manual wiring — draw ctx could auto-bind to engine sprite pass.
- [RENDERING] render.draw uses raw VkCommandBuffer + Vulkan calls directly. Mel_Gpu_Cmd wrappers exist (gpu.cmd.h) but render.draw doesn't use them. Reconcile when gpu.cmd becomes the standard path.

## Friction from sprite.pass / render list ephemeral mode implementation (Mar 2026)

- [RENDERING] `mel_sort_key_sprite` depth component sorts front-to-back (near first), which is wrong for alpha-blended sprites that need back-to-front. Currently harmless because all demos use depth=0 and rely on layer ordering. If depth is ever used with transparent sprites, results will be incorrect. Options: rename to make semantics explicit, flip to match transparent, or document that depth=0 is the expected pattern for 2D sprites.
- [RENDERING] pre-rendered framebuffer mode (`mel_draw_fb_*`) not implemented — design doc describes draw-to-texture with explicit invalidation. Deferred, no consumer yet

## Friction from draw ctx text + ECS text component (Mar 2026)

- [RENDERING] draw ctx rebuilds grid every frame in demo.snake since text changes each frame — previously grid was committed once and reused. Acceptable (sprite pass does same thing), but a dirty flag could skip GPU upload when nothing changed
- [ECS] `mel_text_system_run` creates and destroys a query per call — simple but wasteful. Should cache the query if called every frame. Deferred until there's a consumer that needs it
- [ECS] `Mel_CText.text` is a non-owning str8 — caller must keep the backing data alive. Fine for string literals, but dynamic text (e.g. score counter) needs arena-allocated strings per frame. Document this or add an owned-text variant later

## Street Carlos (MUGEN fighting game demo)

- [STREET-CARLOS] Hit collision system — hitbox/hurtbox overlap detection between fighters, populate Mugen_GetHitVar from HitDef_Result, enter hit reaction states (5000+). Currently hitboxes are computed and drawn but no collision check exists. This is the blocker for combat.
- [STREET-CARLOS] Helper system — MUGEN helpers (Helper state controller, helper tracking, NumHelper query). Currently IsHelper reads `is_helper` field (always false for root chars), NumHelper returns 0. Both correct for now but need real infrastructure for poi-son's helper-based moves.
- [STREET-CARLOS] Projectile system — Projectile state controller, projectile tracking, NumProjID query. Currently NumProjID returns 0 (correct: no projectiles exist). poi-son uses NumProjID(1255) to limit projectile spawning.
- [STREET-CARLOS] GetHitVar defaults from Ikemen-GO reference — hittime defaults to -1 (not 0), yaccel defaults to 0.35/localscale, fall_yvelocity defaults to -4.5/localscale. Currently all zero-initialized.

## Animation System Stubs

- [ANIMATION][STUB] Negative speed (reverse playback): speed field exists and positive values work. Negative values are not handled in mixer update — needs edge case testing for events and looping direction
- [ANIMATION][STUB] Clip serialization: no save/load. Blocked on serialization system

## Animation: implementation gaps/decisions (Spine parity implementation, Feb 2026)

- [ANIMATION] mel__mixer_get_or_add_output uses first-writer-wins for blend function — if two tracks animate the same property_id with different Mel_Blend_Fn (e.g. one angle, one NULL), whichever track gets applied first in a frame sets the blend fn. Subsequent tracks reuse the existing output without updating blend. Undocumented, potentially surprising
- [ANIMATION] keyframe events only fire for l->clip, not the mix_from chain — clips that are fading out during crossfade do not fire their keyframe events. Matches Spine behavior (fading-out timelines don't trigger events) but is a conscious design choice that should be documented
- [ANIMATION] Mel_Anim_Mixer_Output.value is f32[4] — hardcoded max stride. Mixer cannot handle properties wider than 4 floats (e.g. 3x3 matrix). mel_anim_mixer_set_default asserts stride<=4. This is a fundamental limitation
- [ANIMATION] pending_events cleared at top of mel_anim_mixer_update — lifecycle events fired by play()/stop() between updates are lost if update() is called before flush_events(). Not a bug (existing behavior) but a footgun worth documenting
- [ANIMATION] prune_chain only prunes children of completed sub-crossfades, not the entry itself — completed intermediate entries linger in the chain with alpha=1.0. Correct behavior (entry still needed as "old side" of parent crossfade) but wastes recursion cycles with many rapid play() calls. Could be optimized

## Verifications:

- [ANIMATIONS] 100% feature parity with /Users/gabbo/repo/suck/spine-runtimes/spine-c/
- [ASYNC] 100% feature parity with /Users/gabbo/repo/suck/sx/include/sx/fiber.h and jobs.h
- [NATIVE UI] 100% feature parity with /Users/gabbo/repo/suck/nappgui\_src/src/nappgui.h (and SDL\_Window)
- [UI] [LAYOUT] 100% feature parity with /Users/gabbo/repo/suck/nanogui/
- [RENDERING] 100% reimplementation of the following nvidia examples:
 - /Users/gabbo/repo/suck/RTXDI/
 - /Users/gabbo/repo/suck/RTXGI/
 - /Users/gabbo/repo/suck/RTXMG/
 - /Users/gabbo/repo/suck/RTXNS/
 - /Users/gabbo/repo/suck/RTXNTC/
 - /Users/gabbo/repo/suck/RTXPT/
 - /Users/gabbo/repo/suck/RTXTF/
 - /Users/gabbo/repo/suck/RTXTS/
- [ALLOCATORS] 100% feature parity with /Users/gabbo/repo/suck/VMem/
- [DEMOS] 100% reimplementation of these examples: /Users/gabbo/repo/suck/island/apps/examples/
- [RENDER GRAPH] 100% feature parity with /Users/gabbo/repo/suck/pumex

## Higher level systems:
- Gyms
- Zoos
- Museums
