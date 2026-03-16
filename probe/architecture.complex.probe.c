/*
 * GPU-DRIVEN PIPELINE PROBE
 * -------------------------
 * Demonstrates how "Complexity" is just a chain of nodes.
 * The core engine doesn't know what "Nanite" or "Culling" is.
 */

#include "architecture.core.probe.h"

void complex_scene_render(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_Render_Graph* g = mel_render_graph_get();
    Mel_Gpu_Texture* backbuffer = mel_swapchain_acquire(s_swapchain);

    // 1. EXTRACT SCENE (Once)
    // Instances are pushed to a GPU-resident storage buffer.
    mel_stream_clear(&s_instance_stream);
    mel_ecs_extract_meshes(&s_world, &s_instance_stream);

    // 2. THE PIPELINE (Modular Builder)
    // This helper adds multiple nodes: [Cull] -> [Depth Pre] -> [Opaque]
    // The Graph DAG automatically inserts barriers between them.
    mel_add_gpu_mesh_pipeline(g, S8("main_world"),
        .instances = &s_instance_stream,
        .camera = &s_player_camera,
        .target = s_hdr_texture,
        .use_hi_z = true // Injects an extra feedback node!
    );

    // 3. POST PROCESSING
    // Post-processing is just another node that reads from HDR and writes to backbuffer.
    mel_add_tonemap_pass(g, S8("tonemap"),
        .input  = s_hdr_texture,
        .output = backbuffer
    );

    // 4. VR / MULTI-VIEW (The Flex)
    // To support VR, we just add the pipeline again with a different camera!
    // The Graph sees that both pipelines read from 's_instance_stream'.
    // It will run the culling nodes in parallel if the GPU supports it.
    if (vr_enabled) {
        mel_add_gpu_mesh_pipeline(g, S8("right_eye"),
            .instances = &s_instance_stream,
            .camera = &s_right_camera,
            .target = s_vr_texture
        );
    }

    // 5. EXECUTE
    mel_render_graph_execute(g);
    mel_swapchain_present(s_swapchain);
}
