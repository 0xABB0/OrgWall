- [CORE] implement stacktrace capturing for every allocation (debug mode) — DISCUSS: storage format (inline in tracking header vs linked list), debug-only toggling, interaction with Tracy memory profiling. Infrastructure ready (mel\_backtrace\_capture exists).
- [CORE] handle init failure cleanup — PARTIALLY DONE: gpu.device, gpu.swapchain, render.frame return bool, engine init has goto-based cleanup. Remaining: imgui init failure doesn't fully unwind (skips the feature flag)
- [CORE] implement Virtual File System (VFS) with mount points and archive support
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
- [RENDER] implement render graph (data-driven passes)
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
- [BUILD] ~~the build file is becoming too large, needs simplification~~ DONE: melody built as libmelody.a, build_main/build_demo simplified, collect_lib_obj_paths removed
- [DEMO] make a demo with a ton of animated sprites (same atlas, different frame, different parameters - size, direction, tint)
- [DEMO] demo names should be more descriptive (eg demo.native.hello.\*) — file rename from demo\_* to demo.\* is done
- [ENGINE] the engine should expose an "editor" system as entrypoint for every editor to register into. Multiple editors open at any time (even multiple instances of the same editor, pointing to different things).
- [ENGINE] make melody run "headless" (two modes: no graphics API at all, or no windows). Needed for testing vulkan (output to png?). Could also be "run for x frames".
- [ENGINE] unify window creation — currently multiple ways (SDL direct vs ui.native.window.\*)
- [ENGINE] variable rate updates and fixed updates at the same time (discuss: multiple fixed steps?)
- [NATIVE UI][SDL] unify window creation: integrate ui.native.window.\* with sdl
- [ALLOCATOR] growable allocators (block, heap, others?) should all be backed by vmem
- [PROFILING] Tracy coverage expanded (allocator.tracking, gpu.submit, gpu.buffer, texture, render.frame). Still need: more coverage across the codebase, and profiling graphics API calls
- [PROFILING] debug ui (imgui) with overview of total memory, ecs debugging, etc.
- [UPSTREAM] imgui SDL3 backend loses mouse capture during viewport drag/resize (ocornut/imgui#8591, #8869) — known upstream bug, track and update local copy when patched.
- [h files] header inclusion cleanup — MOSTLY DONE: 17 headers fixed to use .fwd.h, 4 new .fwd.h files created. Remaining: gpu.\*.h chain (8 files include gpu.device.h for Vulkan/VMA types transitively — needs architectural discussion about a separate vulkan types header)
- [PHYSICAL STRUCTURE] higher-level files (eg ui.widget.[c|h|.fwd.h]) should sort alphabetically above sub-modules (eg ui.widget.button.\*). Currently *.button comes before *.c
- [OLD FILES] assets.\* needs restructuring into VFS + async I/O — blocked on designing both those modules
- [DISCUSS] is there a way to test for bad behaviour in memory access?
- [DEMO] we need a demo for widget animations
- [DEMO] we need a demo for spritesheet animations
- [DEMO] we need a demo for cutscenes
- [DEMO] we need a demo for sprites
- [STRING] Porter Stemmer algorithm
- [STRING] String builder
- [RENDERING] default white texture should be always available as the "zero asset" (invalid/default texture), not manually created per demo/app
- [RENDERING] Mel_Font_Atlas_Entry should be internal — font draw should take a handle, not force users to fish out the entry
- [ENGINE] Mel_Engine should not hold a reference to SDL_Window — already discussed multiple times, still there
- [DEMO] we need to split demos and examples. demos only show one piece of the engine, examples show the full engine (discuss)
- [EXAMPLE] we need to make a boomer shooter example
- [EXAMPLE] we need to make a 3d example
- [EXAMPLE] we need an example that merges 2d with 3d
- [DEMO] text animations
- [FIX][DEMO] demo trie is broken. it flickers
- [FIX][DEMO] demo pathfind is broken. when it renders without walls, there are a ton of artefacts, and the grid breaks.
- [ENGINE] the game should automatically automatically handle simulations
- ~~[EVENTS] can we merge animation events with our own event system?~~ DONE — anim.event module deleted, mixer uses Mel_Event_Channel directly. Events are collected into pending array during update, flushed to channel subscribers via mel_anim_mixer_flush_events.
- [ENGINE] the animation mixer could be extracted to be something more generic? like, could it be used for the audio system? — ANSWERED: No. Audio and animation have fundamentally different data flows. Animation is property-per-track, audio is sample streams. Curves (math.curve) and state machine (anim.state) can be shared, but the mixer itself should stay animation-specific.
- ~~[ANIMATION] animation defines its own curves. could those be made generic?~~ DONE — moved to math.curve module. Mel_Bezier, mel_bezier_init, mel_curve_eval, MEL_CURVE_LINEAR/STEPPED/BEZIER.
- [LOG] this logged line "Vulkan library load: Vulkan loader library already loaded (continuing — window may have loaded it)" is fucking abhorrent and 300% ai slop.

## Friction from demo.anim (Feb 2026)

- [FIX][SPRITE] sprite batch vertex/index buffer race with 2 frames in flight — single shared buffer is overwritten by frame N while frame N-1's GPU is still reading it. Causes visible glitches during state transitions (wrong UVs rendered with wrong texture). Fix: per-frame vertex/index buffers (one set per frame-in-flight), cycled via frame index. Affects all demos using sprite batch. → update: sprite.batch.h/.c, all demos

## Friction from demo.breakout / demo.pathfind / demo.anim (Feb 2025)

- [SPRITE] shader source string (~40 lines of Slang HLSL) is copy-pasted identically into every demo/app. Provide a built-in default sprite shader (precompiled SPIR-V or shared constant). → update: all demos, demo.tetris.c, demo.snake.c
- [SPRITE] pipeline + vertex binding + attribute + descriptor setup is ~30 lines of identical boilerplate per demo. Sprite batch should own or provide a default pipeline setup (eg `mel_sprite_batch_init_with_default_pipeline`). → update: sprite_batch.h/.c, all demos
- [SPRITE] no `mel_sprite_batch_draw_uv` variant that takes rect/UV structs — passing 8 separate floats is verbose. Consider a struct-based overload. → update: sprite_batch.h/.c
- [FONT] font atlas init is 5 steps of boilerplate every time (pool init, load, get entry, alloc descriptor, write texture). Should be reducible to 1-2 calls. → update: font.atlas.h/.c, all demos
- [FONT] `mel_font_atlas_draw_text` silently switches the sprite batch's active texture. Callers must manually switch back afterward. This violates MEL-COMMAND-V (no hidden intentions). → update: font.atlas.c or sprite_batch.c (push/pop texture?)
- [FONT] `Mel_Font_Atlas_Entry` is exposed to users just so they can pass it to draw. Font draw should take a handle instead. (duplicate of existing [RENDERING] item but adding context)
- [ASYNC] no way to cancel/abort a running coroutine externally. Only option is `mel_coro_destroy` on the entire context. Need `mel_coro_cancel` that returns a coroutine to the free list. → update: async.coro.h/.c
- [GPU] texture struct doesn't expose width/height at the top level — must dig into `Mel_Gpu_Image` internals. → update: gpu.texture.h
- [RENDERING] coordinate system is confusing: ortho(0, w, 0, h) suggests Y-up but sprites render Y-down. Worth documenting or standardizing. → update: docs or sprite_batch
- [RENDERING] no built-in letterboxing/scaling for fixed-resolution games. Ortho projection uses swapchain extent which may differ from design resolution. → update: engine.h or new render utility

## Friction from demo.easing / demo.rbtree / demo.skiplist / demo.trie (Feb 2025)

- [RENDERING] no line drawing primitive in sprite batch — only axis-aligned rects. Curves are drawn as dot arrays (easing), tree/graph edges as L-shaped connectors (rbtree, skiplist, trie). Need at minimum `mel_sprite_batch_draw_line(batch, x1, y1, x2, y2, thickness, color)` for arbitrary-angle lines. → update: sprite_batch.h/.c, all demos
- [TRIE] no "collect all words with prefix" API — `mel_trie_starts_with_str` returns bool only. demo.trie.c had to manually walk `Mel_TrieNode` internals (children, child_keys, child_count, has_value) via custom DFS (~50 lines). Need `mel_trie_collect_prefix_str` or similar. → update: collection.trie.h/.c
- [COLLECTION] `void*` key pattern requires verbose `(void*)(intptr_t)value` casts on every insert/find/remove. Consider convenience macros like `mel_rbtree_insert_int` for common integer key case. → update: collection.rbtree.h, collection.skiplist.h, collection.trie.h
- [INPUT] SDL3 scancode ordering gotcha: SDL_SCANCODE_0 (39) comes AFTER SDL_SCANCODE_9 (38), making naive `>= _0 && <= _9` range checks always false (caught by -Wtautological-overlap-compare). Same for keypad. Consider `mel_scancode_to_digit` utility. → update: new input utility or docs


## Animation System Stubs

- ~~[ANIMATION][STUB] MEL_ANIM_MIX_ADD: constant and field exist, mixer update only implements REPLACE. ADD is trivial but needs a real use case to test properly~~ DONE — ADD blend mode implemented in mel__mixer_apply_layer, tested with two-layer base+additive
- [ANIMATION][STUB] Negative speed (reverse playback): speed field exists and positive values work. Negative values are not handled in mixer update — needs edge case testing for events and looping direction
- [ANIMATION][STUB] Clip serialization: no save/load. Blocked on serialization system
- ~~[ANIMATION] demo.anim.c is broken — needs migration to new animation system~~ DONE — migrated to clip/mixer/state system
- ~~[ANIMATION] demo.breakout.c is broken — uses old anim.sprite API, needs migration~~ DONE — migrated to clip/track direct evaluation
- [ANIMATION] sprite.sheet.h built-in Mel_AnimationPlayer should eventually migrate to anim.sprite

## Animation: missing tests (Spine parity implementation, Feb 2026)

- ~~[TEST] mel_blend_quat_slerp — implemented but zero tests~~ DONE — quat_slerp_basic (identity→90°Z, verifies 45°Z result + unit length) and quat_slerp_antipodal (dot<0 flip path)
- ~~[TEST] stop() firing INTERRUPTED~~ DONE — stop_fires_interrupted test (play, advance, stop, verify INTERRUPTED event)
- ~~[TEST] default values with custom blend function~~ DONE — default_values_with_custom_blend (mel_blend_angle on default, verifies shortest-path angle interpolation during crossfade, not naive lerp)
- ~~[TEST] queued transition with crossfade~~ DONE — queued_transition_with_crossfade (queue with mix_duration>0, verifies mix_from entry exists mid-crossfade, freed after completion)
- ~~[TEST] ADD blend mode with partial weight~~ DONE — add_blend_partial_weight (ADD layer weight=0.5, verifies 50 + 20*0.5 = 60)
- ~~[TEST] ADD blend mode during crossfade~~ DONE — add_blend_during_crossfade (REPLACE layer crossfading + ADD layer on top)
- ~~[TEST] nested crossfade intermediate blending values~~ DONE — nested_crossfade_intermediate_values (three clips A/B/C with overlapping crossfades, verifies intermediate weighted blend)
- ~~[TEST] multiple queued entries~~ DONE — multiple_queued_entries (A→B→C queue, verifies sequential playback and queue draining)

## Animation: implementation gaps/decisions (Spine parity implementation, Feb 2026)

- [ANIMATION] mel__mixer_get_or_add_output uses first-writer-wins for blend function — if two tracks animate the same property_id with different Mel_Blend_Fn (e.g. one angle, one NULL), whichever track gets applied first in a frame sets the blend fn. Subsequent tracks reuse the existing output without updating blend. Undocumented, potentially surprising
- [ANIMATION] keyframe events only fire for l->clip, not the mix_from chain — clips that are fading out during crossfade do not fire their keyframe events. Matches Spine behavior (fading-out timelines don't trigger events) but is a conscious design choice that should be documented
- [ANIMATION] Mel_Anim_Mixer_Output.value is f32[4] — hardcoded max stride. Mixer cannot handle properties wider than 4 floats (e.g. 3x3 matrix). mel_anim_mixer_set_default asserts stride<=4. This is a fundamental limitation
- [ANIMATION] pending_events cleared at top of mel_anim_mixer_update — lifecycle events fired by play()/stop() between updates are lost if update() is called before flush_events(). Not a bug (existing behavior) but a footgun worth documenting
- ~~[ANIMATION] Mel_Anim_Layer.last_event_index — dead field~~ DONE — removed from struct and all initialization sites
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
