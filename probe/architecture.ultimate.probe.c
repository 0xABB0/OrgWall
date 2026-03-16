/*
 * MELODY RENDERING ARCHITECTURE - WORKSHOP V2
 * ============================================
 *
 * Inspired heavily by Blender's draw system (DRW), adapted for Melody's
 * vision: ECS-driven, runtime-extensible, GPU-driven by default,
 * multi-window with independent cadences.
 *
 * BLENDER PATTERNS WE'RE STEALING:
 * - DrawEngine vtable (init -> begin_sync -> object_sync -> end_sync -> draw)
 * - draw::Manager (handle-indexed GPU storage buffers for per-object data)
 * - View-agnostic passes (same pass submitted against multiple views)
 * - GPU-driven visibility (compute shader culling, bitfield output)
 * - Per-viewport engine instances (not singletons)
 * - Sync/draw split (extraction/submission)
 *
 * WHERE WE DIVERGE FROM BLENDER:
 * - Runtime registration of pipelines, not compile-time
 * - Multiple ECS worlds, not one depsgraph per viewport
 * - Independent cadences per window (not single-threaded main loop)
 * - Explicit context passing, not thread-local singleton
 * - Handles everywhere, minimal raw pointers
 *
 * =========================================================================
 *
 *
 * CONCEPT 1: RENDER SOURCE
 * ========================
 *
 * The rendering system does not care WHERE render objects come from.
 * It consumes Mel_Render_Objects. How those get produced is the user's
 * choice.
 *
 * The engine provides SOURCES — adapters that produce render objects
 * from various data structures:
 *
 * SOURCE A: ECS World (the default, zero-effort path)
 *
 *     Mel_World* world = mel_world_create();
 *     ecs_entity_t player = ecs_new(world);
 *     ecs_set(world, player, Mel_Transform2D, { .pos = {100, 200} });
 *     ecs_set(world, player, Mel_Sprite, { .sheet = sheet, .frame = 0 });
 *
 *     // View observes the ECS world. Engine auto-extracts.
 *     mel_view_create(&(Mel_View_Desc){
 *         .source = mel_source_ecs(world),
 *         ...
 *     });
 *
 * SOURCE B: Manual push (for things that don't fit ECS)
 *
 *     // Particle system, procedural geometry, debug drawing, etc.
 *     // The user pushes render objects directly.
 *     Mel_Render_Source* src = mel_source_manual_create();
 *
 *     // Each frame, in the sim tick:
 *     mel_source_manual_begin(src);
 *     for (u32 i = 0; i < particle_count; ++i) {
 *         mel_source_manual_push(src, &(Mel_Render_Object){
 *             .transform = particles[i].transform,
 *             .material  = particle_material,
 *             .mesh      = particle_mesh,
 *         });
 *     }
 *     mel_source_manual_end(src);
 *
 *     mel_view_create(&(Mel_View_Desc){
 *         .source = src,
 *         ...
 *     });
 *
 * SOURCE C: Composite (multiple sources feeding one view)
 *
 *     // A game world (ECS) + debug overlay (manual) + particles (manual)
 *     Mel_Render_Source* combined = mel_source_composite_create();
 *     mel_source_composite_add(combined, mel_source_ecs(game_world));
 *     mel_source_composite_add(combined, debug_draw_source);
 *     mel_source_composite_add(combined, particle_source);
 *
 *     mel_view_create(&(Mel_View_Desc){
 *         .source = combined,
 *         ...
 *     });
 *
 * SOURCE D: Custom (user-defined extraction)
 *
 *     // The user implements a source that can iterate over anything:
 *     // a scene graph, a spatial hash, a flat array, whatever.
 *     Mel_Render_Source_Type my_source_type = {
 *         .name = S8("octree_source"),
 *         .begin = octree_begin,       // called at extraction start
 *         .next  = octree_next,        // yields next Mel_Render_Object, or NULL
 *         .end   = octree_end,         // cleanup
 *         .instance_size = sizeof(Octree_Source_Data),
 *     };
 *
 * The rendering system doesn't know or care about ECS, scene graphs,
 * or any specific data structure. It only sees Mel_Render_Objects
 * coming from a source. ECS is the convenient default, not a requirement.
 *
 *
 * CONCEPT 2: VIEW
 * ===============
 *
 * A View is the central abstraction. It connects:
 *   - A Source (what to render — ECS world, manual push, custom, composite)
 *   - A Camera (from where / how to observe)
 *   - A Target (where to put the pixels)
 *   - A Pipeline (how to turn observations into pixels)
 *
 * Following Blender's pattern, a View also owns:
 *   - Its own Pipeline instance (per-view, not shared/singleton)
 *   - Its own visibility state (GPU culling results)
 *   - Its own view matrices and frustum data
 *
 * A View is a first-class object:
 *
 *     Mel_View* view = mel_view_create(&(Mel_View_Desc){
 *         .source   = mel_source_ecs(game_world),
 *         .camera   = main_camera,
 *         .target   = window_target,
 *         .pipeline = S8("default_2d"),
 *     });
 *
 * CRITICAL PATTERN FROM BLENDER:
 * Each View gets its own pipeline instance. Two views using "default_3d"
 * have two separate instances with independent internal state. This is
 * how multi-window works without shared mutable state.
 *
 * EXAMPLES:
 *
 *   Simple 2D game:
 *     1 world, 1 view, 1 camera, target = swapchain
 *
 *   Split screen:
 *     1 world, 2 views, 2 cameras (different positions),
 *     same window target but different viewport rects
 *
 *   VR:
 *     1 world, 1 view with stereo camera,
 *     target = array image (2 layers, multi-view hardware)
 *
 *   Editor:
 *     1 game world:
 *       - view A: game camera -> game window (pipeline: "default_2d")
 *       - view B: editor camera -> editor window (pipeline: "editor_3d")
 *       - view C: material preview -> small panel (pipeline: "unlit")
 *
 *   Multiple worlds:
 *     World A = game scene, view 1 -> window
 *     World B = UI scene, view 2 -> same window (higher priority)
 *     Composited by view priority ordering
 *
 *
 * CONCEPT 3: CAMERA
 * =================
 *
 * A Camera defines observation parameters. It maps closely to Blender's
 * draw::View (the mathematical frustum), not the camera object.
 *
 *   - View matrix (position/orientation, or derived from a transform)
 *   - Projection (perspective, ortho, custom)
 *   - Viewport rect (portion of the target to render into)
 *   - Visibility mask (which entity groups/layers to see)
 *   - LOD bias / distance overrides
 *
 * For VR, a stereo camera holds multiple view matrices (like Blender's
 * UniformArrayBuffer<ViewMatrices, DRW_VIEW_MAX>). The view uses
 * multi-view extensions to render all eyes in one pass.
 *
 * A Camera is just data. Multiple views can share a camera.
 *
 *
 * CONCEPT 4: TARGET
 * =================
 *
 * A Target is where pixels end up. Maps to Blender's GPUViewport:
 * a pure GPU resource container that owns output textures and
 * framebuffers. Knows nothing about scenes or cameras.
 *
 * Types:
 *   - Window swapchain (auto-resizes with window)
 *   - Offscreen image (render-to-texture)
 *   - Array image (VR, cubemap faces)
 *
 * Multiple views can write to the same target (split screen, UI overlay).
 * The rendering system manages format negotiation and resize handling.
 *
 *
 * CONCEPT 5: PIPELINE
 * ===================
 *
 * A Pipeline defines HOW a view turns observations into pixels.
 * This is Blender's DrawEngine vtable, adapted for Melody.
 *
 * THE INTERFACE (stolen from Blender's DrawEngine):
 *
 *     typedef struct Mel_Pipeline_Type {
 *         str8 name;
 *
 *         void (*init)(Mel_Pipeline* self, Mel_View* view);
 *
 *         void (*begin_sync)(Mel_Pipeline* self);
 *
 *         void (*object_sync)(Mel_Pipeline* self,
 *                             Mel_Render_Object* obj,
 *                             Mel_Render_Manager* mgr);
 *
 *         void (*end_sync)(Mel_Pipeline* self);
 *
 *         void (*draw)(Mel_Pipeline* self,
 *                      Mel_Render_Manager* mgr);
 *
 *         usize instance_size;
 *     } Mel_Pipeline_Type;
 *
 * LIFECYCLE (per frame, per view):
 *
 *     pipeline->init(view)           // setup for this frame
 *     pipeline->begin_sync()         // prepare passes for population
 *     for each visible object:
 *         pipeline->object_sync(obj, mgr)  // register draw calls into passes
 *     pipeline->end_sync()           // finalize, dispatch GPU resource prep
 *     pipeline->draw(mgr)            // submit passes against this view
 *
 * RUNTIME REGISTRATION (divergence from Blender):
 *
 *     Mel_Pipeline_Type my_pipeline = {
 *         .name = S8("stylized_toon"),
 *         .init = toon_init,
 *         .begin_sync = toon_begin_sync,
 *         .object_sync = toon_object_sync,
 *         .end_sync = toon_end_sync,
 *         .draw = toon_draw,
 *         .instance_size = sizeof(Toon_Pipeline_Data),
 *     };
 *     mel_pipeline_register(&my_pipeline);
 *
 * This is runtime, not compile-time. Games, mods, plugins can register
 * new pipelines. The engine ships defaults:
 *
 *   "default_2d"   - sprite batching, alpha sort, simple lighting
 *   "default_3d"   - deferred, PBR, SSAO, TAA, bloom
 *   "unlit"        - minimal, no lighting, no post-process
 *   "overlay"      - debug visualization, gizmos, wireframes
 *
 * ESCAPE HATCHES:
 *
 * The default pipelines are built from composable stages internally.
 * The user can:
 *   1. Use a built-in pipeline as-is (zero effort)
 *   2. Register a fully custom pipeline (full control)
 *   3. (future) Compose a pipeline from stage building blocks
 *
 *
 * CONCEPT 6: RENDER MANAGER
 * =========================
 *
 * Stolen directly from Blender's draw::Manager. This is the GPU-side
 * object database that bridges extraction and submission.
 *
 * The Manager owns handle-indexed storage buffers:
 *
 *     Mel_Render_Manager {
 *         // GPU storage buffers, indexed by Mel_Render_Handle
 *         StorageBuffer  transforms;    // mat4 model + model_inverse per handle
 *         StorageBuffer  bounds;        // AABB per handle (for GPU culling)
 *         StorageBuffer  infos;         // material ID, flags, etc. per handle
 *
 *         // Visibility state per view
 *         // (computed by GPU culling, consumed by command generation)
 *     };
 *
 * During sync, each object gets a Mel_Render_Handle:
 *
 *     void my_object_sync(Mel_Pipeline* self, Mel_Render_Object* obj,
 *                         Mel_Render_Manager* mgr)
 *     {
 *         Mel_Render_Handle h = mel_render_manager_handle(mgr, obj);
 *         // h now indexes into transforms[], bounds[], infos[]
 *         // Register draw calls into passes using this handle
 *         mel_pass_add_draw(&self->main_pass, h, obj->mesh_batch);
 *     }
 *
 * GPU CULLING (Blender's compute_visibility):
 *
 *     mel_render_manager_cull(mgr, view);
 *     // Dispatches compute shader:
 *     //   reads bounds[] + view frustum planes
 *     //   writes visibility bitfield
 *     // Result consumed by command generation
 *
 * The Manager is per-view (each view has its own instance).
 * Double-buffered so previous frame's data is available (for temporal effects).
 *
 *
 * CONCEPT 7: PASSES
 * =================
 *
 * Stolen from Blender's PassMain/PassSimple/PassSortable.
 *
 * KEY INSIGHT: Passes are view-agnostic.
 *
 * Objects register draw calls into passes during sync.
 * Passes are submitted against a specific view during draw.
 * The same pass can be submitted against multiple views
 * (VR stereo, cubemap faces, split screen).
 *
 * Pass types:
 *
 *   Mel_Pass_Batched  - GPU indirect draws. Full GPU-side batching,
 *                       visibility-driven. The fast path. (Blender's PassMain)
 *
 *   Mel_Pass_Direct   - CPU-side command recording. Deterministic order,
 *                       no GPU culling optimization. For small fixed
 *                       draw counts. (Blender's PassSimple)
 *
 *   Mel_Pass_Sorted   - Like Batched but sub-passes sorted by a float key
 *                       before submission. For transparency.
 *                       (Blender's PassSortable)
 *
 * Usage inside a pipeline:
 *
 *     typedef struct My_Pipeline_Data {
 *         Mel_Pass_Batched  opaque_pass;
 *         Mel_Pass_Sorted   transparent_pass;
 *         Mel_Pass_Direct   fullscreen_pass;  // post-process
 *     } My_Pipeline_Data;
 *
 *     void my_begin_sync(Mel_Pipeline* self) {
 *         My_Pipeline_Data* data = self->instance;
 *         mel_pass_batched_clear(&data->opaque_pass);
 *         mel_pass_sorted_clear(&data->transparent_pass);
 *     }
 *
 *     void my_object_sync(Mel_Pipeline* self, Mel_Render_Object* obj,
 *                         Mel_Render_Manager* mgr) {
 *         My_Pipeline_Data* data = self->instance;
 *         Mel_Render_Handle h = mel_render_manager_handle(mgr, obj);
 *
 *         if (obj->material->is_transparent) {
 *             mel_pass_sorted_add(&data->transparent_pass, h,
 *                                 obj->mesh_batch, obj->sort_depth);
 *         } else {
 *             mel_pass_batched_add(&data->opaque_pass, h, obj->mesh_batch);
 *         }
 *     }
 *
 *     void my_draw(Mel_Pipeline* self, Mel_Render_Manager* mgr) {
 *         My_Pipeline_Data* data = self->instance;
 *         Mel_View* view = self->view;
 *
 *         mel_render_manager_cull(mgr, view);
 *
 *         mel_render_manager_generate_commands(mgr, &data->opaque_pass, view);
 *         mel_render_manager_submit(mgr, &data->opaque_pass, view);
 *
 *         mel_render_manager_generate_commands(mgr, &data->transparent_pass, view);
 *         mel_render_manager_submit(mgr, &data->transparent_pass, view);
 *
 *         // Fullscreen post-process (no culling needed)
 *         mel_pass_direct_submit(&data->fullscreen_pass);
 *     }
 *
 *
 * CONCEPT 8: EXTRACTION (THE SYNC PHASE)
 * =======================================
 *
 * Extraction bridges sim and render. In Blender, this is the
 * begin_sync -> object_sync -> end_sync cycle.
 *
 * THE FLOW:
 *
 *     1. For each active source (grouped across views sharing a source):
 *        a. Call source->begin() to start iteration
 *        b. Iterate: source->next() yields Mel_Render_Objects
 *        c. For each view using this source:
 *           - Call pipeline->object_sync(obj, mgr) for every object
 *        d. Call source->end()
 *
 *     2. Each pipeline registers draw calls into its passes during object_sync
 *
 * OPTIMIZATION: When multiple views share the same source, extraction
 * (iterating the source) runs ONCE. Each view still does its own
 * object_sync (because each pipeline instance has its own passes and state).
 *
 * For ECS sources, the built-in extractor queries for entities with visual
 * components and produces Mel_Render_Objects automatically. The user never
 * thinks about this in the simple path.
 *
 *
 * CONCEPT 9: MATERIAL BASE
 * ========================
 *
 * A Material Base defines a "kind of visual" — the link between
 * extracted data and GPU shaders.
 *
 * The engine ships defaults:
 *   - Sprite (2D quad, atlas UV, tint)
 *   - PBR (albedo, normal, metallic, roughness, emissive)
 *   - Unlit (color/texture, no lighting)
 *
 * A custom material base is registered at runtime:
 *
 *     Mel_Material_Base_Desc desc = {
 *         .name = S8("glitch"),
 *         .param_size = sizeof(Glitch_Params),
 *         .shader = glitch_shader,
 *         .compat = MEL_COMPAT_FORWARD | MEL_COMPAT_DEFERRED,
 *     };
 *     mel_material_base_register(&desc);
 *
 * During object_sync, the pipeline reads the object's material base
 * to decide which pass to register the draw call into, and which
 * shader/pipeline state to use.
 *
 *
 * =========================================================================
 * THE FRAME: PUTTING IT ALL TOGETHER
 * =========================================================================
 *
 * What happens every frame:
 *
 *     1. DETERMINE ACTIVE VIEWS
 *        Walk all views. Skip views whose source is paused or whose target
 *        is not due for a frame (independent cadences!). Group views by source.
 *
 *     2. EXTRACTION (per source)
 *        For each active source, iterate to produce Mel_Render_Objects.
 *        This is shared across all views using this source.
 *
 *     3. SYNC (per view)
 *        For each active view:
 *          a. pipeline->init(view)
 *          b. pipeline->begin_sync()
 *          c. For each Mel_Render_Object from this view's source:
 *               pipeline->object_sync(obj, mgr)
 *          d. pipeline->end_sync()
 *
 *     4. DRAW (per view)
 *        For each active view:
 *          a. Activate the view's target (GPU context / render target)
 *          b. pipeline->draw(mgr)
 *             - GPU culling (compute dispatch)
 *             - Command generation (from visibility bitfield)
 *             - Pass submission
 *          c. Deactivate target
 *
 *     5. PRESENT (per window)
 *        For each window with at least one view that drew this frame:
 *          Composite all views targeting this window (by priority order)
 *          Swap
 *
 * INDEPENDENT CADENCES:
 * Step 1 decides which views are "due" based on their target's refresh
 * rate. A 144Hz window's views run every ~7ms. A 60Hz window's views
 * run every ~16ms. Views targeting the slower window simply skip frames
 * where they're not due. Extraction for a source only runs if at least
 * one view using it is active this frame.
 *
 *
 * =========================================================================
 * CONCRETE EXAMPLE: SIMPLE 2D GAME (THE "150 LINES OF SNAKE" PATH)
 * =========================================================================
 *
 *     Mel_World* world = mel_world_create();
 *
 *     // The user just adds entities with visual components
 *     ecs_entity_t snake = ecs_new(world);
 *     ecs_set(world, snake, Mel_Transform2D, { .pos = {5, 5} });
 *     ecs_set(world, snake, Mel_Sprite, { .color = GREEN });
 *
 *     // One view, default everything
 *     Mel_View* view = mel_view_create(&(Mel_View_Desc){
 *         .source = mel_source_ecs(world),
 *         .camera = mel_camera_ortho(0, 320, 0, 240),
 *         .target = mel_target_from_window(window),
 *     });
 *     // pipeline defaults to "default_2d" when not specified
 *
 *     // That's it. The frame loop handles extraction, sync, draw, present.
 *     // The user just updates entity components in their sim tick.
 *
 *
 * =========================================================================
 * CONCRETE EXAMPLE: SPLIT SCREEN CO-OP
 * =========================================================================
 *
 *     // Same source, two cameras, two views, same window
 *     Mel_Render_Source* src = mel_source_ecs(game_world);
 *     Mel_View* p1_view = mel_view_create(&(Mel_View_Desc){
 *         .source = src,
 *         .camera = mel_camera_follow(player1, &(Mel_Camera_Desc){
 *             .projection = mel_projection_ortho(0, 320, 0, 240),
 *             .viewport = { 0, 0, 0.5f, 1.0f },  // left half
 *         }),
 *         .target = window_target,
 *         .priority = 0,
 *     });
 *
 *     Mel_View* p2_view = mel_view_create(&(Mel_View_Desc){
 *         .source = src,
 *         .camera = mel_camera_follow(player2, &(Mel_Camera_Desc){
 *             .projection = mel_projection_ortho(0, 320, 0, 240),
 *             .viewport = { 0.5f, 0, 0.5f, 1.0f },  // right half
 *         }),
 *         .target = window_target,
 *         .priority = 0,
 *     });
 *
 *     // Extraction runs ONCE (same source).
 *     // Sync runs TWICE (one per view, each with own pipeline instance).
 *     // GPU culling runs TWICE (different frustums).
 *     // Draw runs TWICE (different viewports on same target).
 *
 *
 * =========================================================================
 * CONCRETE EXAMPLE: AAA-ISH 3D WITH POST-PROCESSING
 * =========================================================================
 *
 *     // The "default_3d" pipeline internally creates:
 *     //   - G-buffer pass (opaque geometry -> multiple render targets)
 *     //   - Shadow pass (depth-only from light POV)
 *     //   - Lighting pass (fullscreen, reads G-buffer + shadows)
 *     //   - Forward pass (transparent objects, sorted)
 *     //   - Post-process chain (SSAO -> TAA -> bloom -> tonemap)
 *     //   - Final composite to target
 *     //
 *     // The user doesn't see any of this:
 *
 *     Mel_View* view = mel_view_create(&(Mel_View_Desc){
 *         .source   = mel_source_ecs(game_world),
 *         .camera   = main_cam,
 *         .target   = window_target,
 *         .pipeline = S8("default_3d"),
 *     });
 *
 *     // To customize, the user can create their own pipeline type
 *     // that reuses internal building blocks, or goes fully custom.
 *
 *
 * =========================================================================
 * CONCRETE EXAMPLE: BINDLESS + MESH SHADERS (GPU-DRIVEN RENDERING)
 * =========================================================================
 *
 * This is the "default_3d" pipeline's internal architecture.
 * The user never sees this — but this is how Melody renders at full power.
 *
 * BINDLESS TEXTURES:
 *
 * All textures live in a single global descriptor array. A material
 * doesn't "bind" textures — it stores indices into this array.
 *
 *     // Engine maintains a global texture table
 *     Mel_Texture_Table {
 *         // One massive descriptor set with VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
 *         // Bound ONCE per frame. Never rebound between draws.
 *         VkDescriptorSet global_textures;
 *         u32             next_slot;
 *     };
 *
 *     // When a texture is loaded, it gets a slot
 *     u32 albedo_slot = mel_texture_table_add(&table, albedo_image, sampler);
 *     u32 normal_slot = mel_texture_table_add(&table, normal_image, sampler);
 *
 *     // Material parameters store indices, not descriptors
 *     typedef struct {
 *         u32 albedo_idx;    // index into global texture array
 *         u32 normal_idx;
 *         u32 roughness_idx;
 *         f32 metallic;
 *     } PBR_Material_Params;
 *
 * In the shader:
 *
 *     layout(set = 0, binding = 0) uniform sampler2D textures[];
 *     // ...
 *     vec4 albedo = texture(textures[material.albedo_idx], uv);
 *
 * Why this matters: ZERO descriptor set switches between draws.
 * Every draw call uses the same descriptor set. The only thing that
 * varies per-draw is the push constant (or storage buffer index)
 * telling the shader which material parameters to read.
 *
 *
 * MESH SHADERS:
 *
 * Traditional vertex pipeline: CPU submits vertex buffers, index buffers,
 * draw calls. Each draw call = one pipeline bind + one vertex buffer bind.
 *
 * Mesh shader pipeline: ALL geometry lives in storage buffers. The mesh
 * shader reads vertices/indices by handle. No vertex input state. No
 * per-draw buffer binds.
 *
 * This maps perfectly to the Manager pattern:
 *
 *     // The Manager's storage buffers (already handle-indexed):
 *     Mel_Render_Manager {
 *         StorageBuffer transforms;   // mat4 per handle
 *         StorageBuffer bounds;       // AABB per handle
 *         StorageBuffer infos;        // material_idx, mesh_idx, flags per handle
 *
 *         // Geometry pool: ALL meshes in one set of buffers
 *         StorageBuffer vertices;     // all vertices, all meshes, concatenated
 *         StorageBuffer meshlets;     // meshlet descriptors (vertex offset, count, etc.)
 *         StorageBuffer meshlet_data; // meshlet triangle indices
 *
 *         // Material pool: ALL material params in one buffer
 *         StorageBuffer materials;    // all PBR_Material_Params, indexed by material_idx
 *     };
 *
 * THE FULL GPU-DRIVEN PIPELINE:
 *
 *     void default_3d_draw(Mel_Pipeline* self, Mel_Render_Manager* mgr) {
 *         Default3D_Data* data = self->instance;
 *         Mel_View* view = self->view;
 *
 *         // STEP 1: GPU CULL (compute shader)
 *         // Input:  bounds[], view frustum
 *         // Output: visible_objects bitfield
 *         mel_render_manager_cull(mgr, view);
 *
 *         // STEP 2: MESHLET CULL (compute shader)
 *         // Input:  visible_objects, meshlets[], view frustum (tighter per-meshlet)
 *         // Output: visible_meshlets bitfield + indirect dispatch args
 *         // This is a SECOND cull pass — first culls objects, then culls
 *         // individual meshlets within visible objects. Backface culling,
 *         // frustum culling, occlusion culling per meshlet.
 *         mel_render_manager_cull_meshlets(mgr, view);
 *
 *         // STEP 3: COMMAND GENERATION (compute shader)
 *         // Input:  visible_meshlets
 *         // Output: VkDrawMeshTasksIndirectCommand buffer
 *         // One indirect command per visible meshlet group.
 *         mel_render_manager_generate_mesh_commands(mgr, &data->gbuffer_pass, view);
 *
 *         // STEP 4: G-BUFFER FILL (mesh shader dispatch)
 *         // The mesh shader reads:
 *         //   - transforms[handle] for the model matrix
 *         //   - infos[handle].material_idx for material lookup
 *         //   - materials[material_idx] for PBR params
 *         //   - vertices[] + meshlets[] for geometry
 *         //   - textures[material.albedo_idx] for bindless textures
 *         // ALL from storage buffers. No vertex input. No descriptor switches.
 *         // One vkCmdDrawMeshTasksIndirectEXT call.
 *         mel_pass_batched_submit(&data->gbuffer_pass);
 *
 *         // STEP 5: LIGHTING (fullscreen compute or fragment)
 *         // Reads G-buffer + shadow maps. Writes lit result.
 *         mel_pass_direct_submit(&data->lighting_pass);
 *
 *         // STEP 6: FORWARD TRANSPARENT (sorted, traditional or mesh shader)
 *         mel_pass_sorted_submit(&data->transparent_pass);
 *
 *         // STEP 7: POST-PROCESS CHAIN
 *         mel_pass_direct_submit(&data->ssao_pass);
 *         mel_pass_direct_submit(&data->taa_pass);
 *         mel_pass_direct_submit(&data->bloom_pass);
 *         mel_pass_direct_submit(&data->tonemap_pass);
 *     }
 *
 * THE FLOW FOR ONE OPAQUE OBJECT:
 *
 *     [object_sync]
 *       handle = manager_handle(mgr, obj)
 *       // Manager writes: transforms[handle] = obj->transform
 *       //                 bounds[handle]     = obj->aabb
 *       //                 infos[handle]      = { material_idx, mesh_idx, ... }
 *       pass_batched_add(&gbuffer_pass, handle, obj->meshlet_range)
 *
 *     [draw]
 *       GPU cull:       bounds[handle] vs frustum -> visible? bit set
 *       Meshlet cull:   meshlets[mesh_idx..] vs frustum -> visible meshlets
 *       Cmd gen:        emit VkDrawMeshTasksIndirect for visible meshlets
 *       Mesh shader:    reads vertices[...], transforms[handle], materials[...]
 *                       samples textures[material.albedo_idx]
 *                       writes to G-buffer
 *
 * Zero CPU-side per-object work during draw. Zero descriptor switches.
 * Zero vertex buffer binds. The CPU submits ONE indirect dispatch and
 * ONE indirect mesh draw. The GPU does everything else.
 *
 * FALLBACK (no mesh shader support):
 *
 * Same Manager, same storage buffers, same bindless textures.
 * Instead of mesh shaders, use traditional vertex pipeline with:
 *   - vkCmdDrawIndexedIndirect (multi-draw indirect)
 *   - Vertex shader pulls from storage buffers by gl_InstanceIndex
 *   - Same material/texture lookup, just different entry point
 *
 * The pipeline detects hardware caps at init and picks the code path.
 * The user's object_sync code is IDENTICAL for both paths — it just
 * registers handles into passes. The pass submission is what differs.
 *
 *
 * =========================================================================
 * CONCRETE EXAMPLE: MIXED SOURCES (ECS + PARTICLES + DEBUG)
 * =========================================================================
 *
 *     // ECS world for the game scene
 *     Mel_Render_Source* scene = mel_source_ecs(game_world);
 *
 *     // Manual source for a custom particle system
 *     Mel_Render_Source* particles = mel_source_manual_create();
 *     // ... push particle render objects each frame ...
 *
 *     // Manual source for debug drawing (wireframes, gizmos)
 *     Mel_Render_Source* debug = mel_source_manual_create();
 *     // ... push debug lines, AABBs, etc. ...
 *
 *     // Composite: all three feed into one view
 *     Mel_Render_Source* everything = mel_source_composite_create();
 *     mel_source_composite_add(everything, scene);
 *     mel_source_composite_add(everything, particles);
 *     mel_source_composite_add(everything, debug);
 *
 *     Mel_View* view = mel_view_create(&(Mel_View_Desc){
 *         .source   = everything,
 *         .camera   = main_cam,
 *         .target   = window_target,
 *         .pipeline = S8("default_3d"),
 *     });
 *
 *     // The pipeline's object_sync sees ALL render objects regardless
 *     // of where they came from. It routes them to the appropriate
 *     // passes based on material type (opaque, transparent, wireframe).
 *
 *
 * =========================================================================
 * OPEN QUESTIONS (ACTIVE)
 * =========================================================================
 *
 * [Q-MANAGER-SCOPE] Is the Manager per-view or per-source?
 *   Per-view means each view has its own GPU buffers (simpler, more memory).
 *   Per-source means views sharing a source share GPU object data and only
 *   diverge at culling (less memory, more complex lifetime).
 *   Blender does per-viewport. With multiple views per source, we need to
 *   think about this carefully.
 *
 * [Q-MULTI-CADENCE] How does independent cadence work in practice?
 *   Option A: main loop runs at fastest cadence, slower views skip frames.
 *   Option B: each window has its own render thread with its own loop.
 *   Option A is simpler. Option B is more correct for truly independent
 *   timing but requires per-thread GPU contexts and careful sync.
 *   Blender does single-thread with one drawable window at a time.
 *   We need better.
 *
 * [Q-COMPOSITE] When multiple views write to the same target, how?
 *   Are they composited by the engine (blend by priority)?
 *   Or do they render into non-overlapping viewport rects?
 *   Or both (some fullscreen with alpha, some viewport-clipped)?
 *
 * [Q-RESOURCE-SHARING] Shadow maps, environment probes, etc. —
 *   these are source-level resources, not view-level. If two views
 *   share a source, can they share shadow maps?
 *   This pushes toward the Manager being per-source for shared resources,
 *   with per-view visibility state layered on top.
 *
 * [Q-RENDER-OBJECT] What does Mel_Render_Object contain?
 *   It's the extracted snapshot of an entity's visual state.
 *   Transform, material base, mesh/sprite data, flags.
 *   How much is copied vs referenced? COW? ImplicitSharing?
 *   This is where Blender's two-layer COW pattern is relevant.
 *
 * [Q-PASS-TARGETS] How do passes within a pipeline connect to
 *   intermediate render targets (G-buffer, shadow map, etc.)?
 *   Are these owned by the pipeline instance? The manager?
 *   How does the post-process chain read the G-buffer?
 *   Blender's pipelines own their intermediate textures as
 *   direct value members of the Instance struct.
 */
