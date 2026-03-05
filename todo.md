--- THE EVER GROWING LIST OF THINGS TO DO ---


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
- [RENDER] ~~implement render graph (data-driven passes)~~ PARTIAL: render.graph.h/c implemented with DAG compilation, barrier computation, per-frame resource ownership, parameterless execute. Remaining: memory aliasing for transient targets, GPU counter reset for write lists, production hooks integration
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
- [PROFILING] Tracy coverage expanded (allocator.tracking, gpu.submit, gpu.buffer, texture, render.graph). Still need: more coverage across the codebase, and profiling graphics API calls
- [PROFILING] debug ui (imgui) with overview of total memory, ecs debugging, etc.
- [UPSTREAM] imgui SDL3 backend loses mouse capture during viewport drag/resize (ocornut/imgui#8591, #8869) — known upstream bug, track and update local copy when patched.
- [h files] header inclusion cleanup — MOSTLY DONE: 17 headers fixed to use .fwd.h, 4 new .fwd.h files created. Remaining: gpu.\*.h chain (8 files include gpu.device.h for Vulkan/VMA types transitively — needs architectural discussion about a separate vulkan types header)
- [PHYSICAL STRUCTURE] higher-level files (eg ui.widget.[c|h|.fwd.h]) should sort alphabetically above sub-modules (eg ui.widget.button.\*). Currently *.button comes before *.c
- ~~[OLD FILES] assets.\* needs restructuring into VFS + async I/O~~ DONE: `assets.*` removed and consumers migrated to VFS
- [DISCUSS] is there a way to test for bad behaviour in memory access?
- [DEMO] we need a demo for widget animations
- [DEMO] we need a demo for spritesheet animations
- [DEMO] we need a demo for cutscenes
- [DEMO] we need a demo for sprites
- [STRING] Porter Stemmer algorithm
- [STRING] String builder
- ~~[RENDERING] default white texture should be always available as the "zero asset" (invalid/default texture), not manually created per demo/app~~ DONE: white texture owned by `Mel_Sprite_Pass` in engine, accessible via `engine->sprite_pass->white_texture`
- ~~[RENDERING] Mel_Font_Atlas_Entry should be internal — font draw should take a handle, not force users to fish out the entry~~ DONE: `mel_font_atlas_draw_text` and `mel_font_atlas_measure_text` now take `(pool, handle)`. `mel_font_atlas_pool_get_texture` added for init-time texture registration. `Mel_Draw_Ctx_Text_Opt` uses `Mel_Font_Handle`. All demos, ECS text system, and UI label migrated
- [ENGINE] migrate legacy `Mel_Engine` + `SDL_Window` coupling to module statics + per-window `Mel_Window` ownership (vNext direction in `design/engine.overview.md`)
- [DEMO] we need to split demos and examples. demos only show one piece of the engine, examples show the full engine (discuss)
- [EXAMPLE] we need to make a boomer shooter example
- [EXAMPLE] we need to make a 3d example
- [EXAMPLE] we need an example that merges 2d with 3d
- [DEMO] text animations
- [FIX][DEMO] demo trie is broken. it flickers
- [FIX][DEMO] demo pathfind is broken. when it renders without walls, there are a ton of artefacts, and the grid breaks.
- ~~[ENGINE] the game should automatically automatically handle simulations~~ PARTIALLY DONE: `Mel_Sim_Ctx` now has full fixed-timestep system (`Mel_Sim_Fixed`, `mel_sim_add_fixed`, `mel_sim_fixed_add_update`), variable updates (`mel_sim_add_variable`), time scale, user data, and `mel_sim_tick` driver. Engine's old `fixed_dt`/`accumulator`/`alpha` fields deleted — `mel_engine_frame` now calls `on_update` once per frame with real `frame_dt`. Remaining: `mel_sim_register`/`mel_sim_unregister` (engine-driven sim scheduling), deferred mutations, demo migration to use `Mel_Sim_Ctx`
- [BUILD] the build system we're using right now (nob.h) is fine, but at this point i think that melody can include that in its own source code i think
- ~~[TEST] i don't like tests having each it's own main. is there a way to augment the testing system so that tests are auto-discovered, with a single main, and possibly something like tagging the tests so we can run just some of them (or even one of them, by like id)?~~ DONE: unified test binary, auto-registration via `__attribute__((constructor))`, tags, --filter/--tag/--id/--list/--visual CLI
- [ALLOC] we should remove the mel\_alloc\_heap() function. everyone that needs to allocate needs to get passed an allocator directly.
- [BUILD] we need to have a way to optimize the release for a certain system/architecture/cpu (march flag, using handcrafted assembly per-feature, whatever)
- [BUILD] we need a better method to define if a source file is platform specific (build system-wise). i don't like having a map of "for this, skip these", maybe it could be made less ugly by doing something like ".osx -> defined(__APPLE__)"
- [RENDER] render graph uses MEL_MAX_FRAMES_IN_FLIGHT, but i feel like i'd prefer to have a dynamically allocated buffer for that
- ~~[RENDER] render.frame.h/c is effectively dead code for graph-driven apps — the render graph now owns per-frame command pools/buffers/fences. Deprecate render.frame or fold into graph. Demos still using the old path (render_frame_begin/end) should migrate to mel_render_graph_execute~~ DONE: render.frame.h/c deleted. All consumers (engine, demos, test.visual, game/main.c) migrated to render graph. on_render callback removed from Mel_App_Opt.
- ~~[ENGINE] mel_engine_frame currently owns the render_frame_begin/end + swapchain lifecycle. Graph-driven apps bypass this entirely. Need to add graph-driven rendering support to the engine frame loop (flag or separate path) so demos don't have to run their own loop~~ DONE: Mel_Engine has render_graph pointer. mel_engine_frame calls mel_render_graph_execute. imgui is now a graph pass in game/main.c (igNewFrame in engine, igRender + RenderDrawData in the imgui pass function).
- [CAMERA] Consider if it's worth having a Camera2D in addition to the Camera we have now, to have an extremly optimized 2d pipeline if needed

## Friction from demo.anim (Feb 2026)

- ~~[FIX][SPRITE] sprite pass vertex/index buffer race with 2 frames in flight — single shared buffer is overwritten by frame N while frame N-1's GPU is still reading it. Causes visible glitches during state transitions (wrong UVs rendered with wrong texture). Fix: per-frame vertex/index buffers (one set per frame-in-flight), cycled via frame index. Affects all demos using sprite pass. → update: sprite.pass.h/.c, all demos~~ DONE: `Mel_Sprite_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT]` with per-frame vertex+index buffers, cycled in `mel__sprite_pass_begin`

## Friction from demo.breakout / demo.pathfind / demo.anim (Feb 2025)

- ~~[SPRITE] shader source string (~40 lines of Slang HLSL) is copy-pasted identically into every demo/app. Provide a built-in default sprite shader (precompiled SPIR-V or shared constant). → update: all demos, demo.tetris.c, demo.snake.c~~ DONE: shader embedded in `Mel_Sprite_Pass` (sprite.pass.c), engine-owned. All 9 demos migrated
- ~~[SPRITE] pipeline + vertex binding + attribute + descriptor setup is ~30 lines of identical boilerplate per demo. Sprite batch should own or provide a default pipeline setup (eg `mel_sprite_batch_init_with_default_pipeline`). → update: sprite_batch.h/.c, all demos~~ DONE: pipeline, batch, and white texture owned by `Mel_Sprite_Pass`, initialized in engine. All 9 demos migrated
- ~~[SPRITE] no `mel_sprite_batch_draw_uv` variant that takes rect/UV structs — passing 8 separate floats is verbose. Consider a struct-based overload. → update: sprite_batch.h/.c~~ DONE: SpriteBatch removed. Mel_Sprite_Entry now has `Mel_Rect uv` and `Mel_Texture_Handle tex` fields. Entries are pushed to render lists with struct-based data.
- ~~[FONT] font atlas init is 5 steps of boilerplate every time (pool init, load, get entry, alloc descriptor, write texture). Should be reducible to 1-2 calls.~~ DONE: `mel_font_atlas_pool_init` takes optional `.texture_pool`. When set, `mel_font_atlas_pool_load` auto-registers the font texture. `mel_font_atlas_draw_text` no longer takes `atlas_tex` param — reads tex_handle from entry. `Mel_CText`, `Mel_WLabel`, `Mel_Draw_Ctx_Text_Opt` all dropped `font_tex` field. Font init is now 2 lines (init pool + load). All 9 demos + game/main.c migrated.
- ~~[FONT] `mel_font_atlas_draw_text` silently switches the sprite batch's active texture. Callers must manually switch back afterward. This violates MEL-COMMAND-V (no hidden intentions). → update: font.atlas.c or sprite_batch.c (push/pop texture?)~~ DONE: font draw now pushes Mel_Sprite_Entry per glyph to a render list with explicit texture handle. No hidden state mutation.
- ~~[FONT] `Mel_Font_Atlas_Entry` is exposed to users just so they can pass it to draw. Font draw should take a handle instead. (duplicate of existing [RENDERING] item but adding context)~~ DONE: see [RENDERING] item above
- [ASYNC] no way to cancel/abort a running coroutine externally. Only option is `mel_coro_destroy` on the entire context. Need `mel_coro_cancel` that returns a coroutine to the free list. → update: async.coro.h/.c
- [GPU] texture struct doesn't expose width/height at the top level — must dig into `Mel_Gpu_Image` internals. → update: gpu.texture.h
- [RENDERING] coordinate system is confusing: ortho(0, w, 0, h) suggests Y-up but sprites render Y-down. Worth documenting or standardizing. → update: docs or sprite.pass
- [RENDERING] no built-in letterboxing/scaling for fixed-resolution games. Ortho projection uses swapchain extent which may differ from design resolution. → update: engine.h or new render utility

## Friction from demo.easing / demo.rbtree / demo.skiplist / demo.trie (Feb 2025)

- ~~[RENDERING] no line drawing primitive in sprite batch~~ DONE: line drawing absorbed into Mel_Sprite_Pass as `mel__sprite_pass_push_line` (file-local). SpriteBatch removed entirely.
- [TRIE] no "collect all words with prefix" API — `mel_trie_starts_with_str` returns bool only. demo.trie.c had to manually walk `Mel_TrieNode` internals (children, child_keys, child_count, has_value) via custom DFS (~50 lines). Need `mel_trie_collect_prefix_str` or similar. → update: collection.trie.h/.c
- [COLLECTION] `void*` key pattern requires verbose `(void*)(intptr_t)value` casts on every insert/find/remove. Consider convenience macros like `mel_rbtree_insert_int` for common integer key case. → update: collection.rbtree.h, collection.skiplist.h, collection.trie.h
- [INPUT] SDL3 scancode ordering gotcha: SDL_SCANCODE_0 (39) comes AFTER SDL_SCANCODE_9 (38), making naive `>= _0 && <= _9` range checks always false (caught by -Wtautological-overlap-compare). Same for keypad. Consider `mel_scancode_to_digit` utility. → update: new input utility or docs


## Friction from render.draw implementation (Mar 2026)

- [RENDERING] Mel_Draw_Vertex duplicates Mel_SpriteVertex binary layout — intentional (shares sprite pipeline/shader). When the material system ships, draw ctx gets its own pipeline and the vertex type diverges. Known coupling. Note: Mel_SpriteVertex is now file-local in sprite.pass.c
- [RENDERING] render.draw takes pipeline/texture/dev externally — partially addressed: demos now use `engine->sprite_pass->pipeline` and `engine->sprite_pass->white_texture` instead of local copies. Still requires manual wiring — draw ctx could auto-bind to engine sprite pass.
- [RENDERING] render.draw uses raw VkCommandBuffer + Vulkan calls directly. Mel_Gpu_Cmd wrappers exist (gpu.cmd.h) but render.draw doesn't use them. Reconcile when gpu.cmd becomes the standard path.
- ~~[RENDERING] render.draw is standalone — doesn't feed into render lists. Design doc (engine.render.md) envisions draw APIs populating render lists. When render lists land, decide: draw ctx becomes a frontend to render lists, or stays as a lower-level escape hatch for direct GPU submission.~~ RESOLVED: draw ctx stays as direct GPU submission (lower-level path). Text rendering added via `mel_draw_ctx_text`, with multi-texture draw cmd batching. demo.snake migrated: font render list replaced with draw ctx text.

## Friction from sprite.pass / render list ephemeral mode implementation (Mar 2026)

- ~~[RENDERING] sprite batch cannot be used for two begin/end cycles in the same frame with the same batch~~ DONE: SpriteBatch removed entirely. Vertex buffer management absorbed into Mel_Sprite_Pass (single begin/flush per execute). Per-frame buffer race still exists (see Friction from demo.anim section)
- ~~[RENDERING] sort key helpers are incomplete — only `mel_sort_key_sprite(layer, depth)` exists. Design doc (engine.render.md) specifies `mel_sort_key_sprite(layer, depth, material, texture_bucket)`, `mel_sort_key_opaque(depth)` (front-to-back), and `mel_sort_key_transparent(depth)` (back-to-front). Blocked on material/bindless system for the material/texture params~~ DONE: `mel_sort_key_sprite(layer, depth, material, texture_bucket)` implemented with layout `[Layer(8) | Depth(24) | Material(16) | Texture(16)]`. `mel_sort_key_opaque(depth)` and `mel_sort_key_transparent(depth)` added.
- ~~[RENDERING] no ephemeral draw convenience — design doc shows `mel_draw_sprite(sprite_list, pos, tex_handle, color)` as a one-liner. Currently users must manually `mel_render_list_push` + fill every field of `Mel_Sprite_Entry`. → update: sprite.pass.h/.c~~ DONE: `mel_draw_sprite(list, .pos = ..., .size = ..., .color = ...)` opt pattern. Zero-init defaults for uv (→ MEL_UV_FULL), layer, depth, tex. demo.trie.c and demo.easing.c migrated as proof of concept
- ~~[RENDERING] sort key helpers incomplete — only `mel_sort_key_sprite` existed. Missing: `mel_sort_key_opaque(depth)` (front-to-back) and `mel_sort_key_transparent(depth)` (back-to-front)~~ DONE: both added to sprite.pass.h. Opaque uses natural float-to-sortable (near first). Transparent inverts (far first).
- [RENDERING] ~~no ECS sync systems for render lists — design doc (engine.render.md) describes default-provided ECS systems with OnAdd/OnSet/OnRemove that reactively maintain render list entries.~~ DONE: `melody/render.sync.h/c` provides `Mel_Render_Sync` using Flecs observers and entity->entry side-tables. Verified in `test_render_sync.c` and integrated into `demo.snake.c`.
- [RENDERING] `mel_sort_key_sprite` depth component sorts front-to-back (near first), which is wrong for alpha-blended sprites that need back-to-front. Currently harmless because all demos use depth=0 and rely on layer ordering. If depth is ever used with transparent sprites, results will be incorrect. Options: rename to make semantics explicit, flip to match transparent, or document that depth=0 is the expected pattern for 2D sprites.
- ~~[RENDERING] no production hooks on render lists — design doc specifies `mel_render_list_add_producer` / `mel_render_list_remove_producer` for attaching per-frame producers to lists. Not implemented~~ DONE: `mel_render_list_add_producer`, `mel_render_list_remove_producer`, `mel_render_list_produce` added. Idempotent per clear cycle (`produced` flag). Render graph calls `produce_lists` on all passes' lists before executing
- ~~[RENDERING] Mel_Sprite_Entry is intentionally minimal — no UV rect, no Mel_Texture_Handle, color is Mel_Vec4 not packed u32. Expand when material/bindless system lands~~ DONE: Mel_Sprite_Entry now has `Mel_Rect uv` and `Mel_Texture_Handle tex`. Color is still Vec4 (target packed u32 deferred)
- [RENDERING] pre-rendered framebuffer mode (`mel_draw_fb_*`) not implemented — design doc describes draw-to-texture with explicit invalidation. Deferred, no consumer yet

## Friction from draw ctx text + ECS text component (Mar 2026)

- [RENDERING] draw ctx rebuilds grid every frame in demo.snake since text changes each frame — previously grid was committed once and reused. Acceptable (sprite pass does same thing), but a dirty flag could skip GPU upload when nothing changed
- ~~[RENDERING] draw ctx buffer race with frames in flight — memcpy into mapped memory while previous frame's GPU is still reading it, plus buffer destruction during growth races with in-flight command buffers~~ DONE: per-frame GPU buffers (`Mel_Draw_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT]`), cycled in `mel_draw_ctx_clear`. Each frame slot has independent vertex+index buffers. Growth in one slot doesn't affect others. Sprite pass still has the same underlying race (tracked separately)
- [ECS] `mel_text_system_run` creates and destroys a query per call — simple but wasteful. Should cache the query if called every frame. Deferred until there's a consumer that needs it
- [ECS] `Mel_CText.text` is a non-owning str8 — caller must keep the backing data alive. Fine for string literals, but dynamic text (e.g. score counter) needs arena-allocated strings per frame. Document this or add an owned-text variant later

## Animation System Stubs

- [ANIMATION][STUB] Negative speed (reverse playback): speed field exists and positive values work. Negative values are not handled in mixer update — needs edge case testing for events and looping direction
- [ANIMATION][STUB] Clip serialization: no save/load. Blocked on serialization system
- [ANIMATION] sprite.sheet.h built-in Mel_AnimationPlayer should eventually migrate to anim.sprite

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
