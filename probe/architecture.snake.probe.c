/*
 * SNAKE PROBE: 150-Line Challenge
 * ------------------------------
 * Demonstrates the Flattened Stack: No Recipes, No Plans, No Stage Ceremony.
 * Pure Simulation -> Stream -> Graph logic.
 */

#include "architecture.core.probe.h"

// 1. Game State
typedef struct { i16 x, y; } Pos;
Pos s_body[256];
Pos s_food;
Pos s_dir = {1, 0};
u32 s_len = 3;

// 2. Logic Step (Simulation Tick)
void snake_tick(Mel_Sim_Ctx* sim, f32 dt) {
    // Process input from stack
    if (mel_input_action(S8("move_up")))    s_dir = (Pos){0, -1};
    if (mel_input_action(S8("move_down")))  s_dir = (Pos){0,  1};
    // ... move logic ...
}

// 3. Render Step (Extraction & Graph Building)
void snake_render(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Render_Graph* g = mel_render_graph_get();
    
    // A. ACQUIRE OUTPUT
    Mel_Gpu_Texture* backbuffer = mel_swapchain_acquire(s_swapchain);

    // B. EXTRACT WORLD -> STREAM
    // Note: s_sprite_stream is a "Module" provided by the engine.
    mel_stream_clear(&s_sprite_stream);
    for(u32 i = 0; i < s_len; ++i) {
        // Interpolate between logic ticks using sim->alpha
        Pos p = lerp_pos(s_body_prev[i], s_body[i], mel_sim_alpha(sim));
        mel_sprite_push(&s_sprite_stream, .pos = {p.x*20, p.y*20}, .color = GREEN);
    }

    // C. BUILD THE GRAPH (The "Plan" is the Code!)
    // mel_add_sprite_pass is a "Module Helper". It adds a Node to the Graph.
    // It internally handles shader binding and buffer mapping.
    mel_add_sprite_pass(g, S8("world_draw"), 
        .stream = &s_sprite_stream, 
        .target = backbuffer, 
        .camera = &s_ortho_cam,
        .clear  = {0.05f, 0.05f, 0.08f, 1.0f}
    );

    // D. EXECUTE & PRESENT
    mel_render_graph_execute(g);
    mel_swapchain_present(s_swapchain);
}

// 4. Entry
void app_init() {
    // Boilerplate-free setup
    mel_gpu_init(); // Auto-selects API
    mel_sim_init(&s_sim);
    mel_sim_add_fixed(&s_sim, snake_tick, 15.0f);
    mel_sim_add_variable(&s_sim, snake_render, 0); // Rendering is just an observer
    
    // Add input context
    mel_input_push_context(&(Mel_Input_Ctx){
        .actions = { {S8("move_up"), KEY_W}, {S8("move_down"), KEY_S}, {0} }
    });
}
