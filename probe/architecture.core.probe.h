#pragma once

/* 
 * MEL_CORE_HAL (Hardware Abstraction Layer)
 * -----------------------------------------
 * Pure vtable-driven API. No #ifdefs.
 */

typedef struct Mel_Gpu_Device  Mel_Gpu_Device;
typedef struct Mel_Gpu_Buffer  Mel_Gpu_Buffer;
typedef struct Mel_Gpu_Texture Mel_Gpu_Texture;
typedef struct Mel_Gpu_Cmd     Mel_Gpu_Cmd;

typedef u32 Mel_Gpu_Access; // Bitfield: READ, WRITE, INDIRECT, etc.
typedef u32 Mel_Format;     // Hashes: S8("RGBA8_SRGB")

// The fundamental unit of work
typedef void (*Mel_Gpu_Node_Fn)(Mel_Gpu_Cmd* cmd, void* user);

/*
 * MEL_RENDER_GRAPH (The Scheduler)
 * --------------------------------
 * The only "Orchestrator". It connects work via data dependencies.
 */

typedef struct {
    str8            name;
    Mel_Gpu_Node_Fn execute;
    void*           user;
    
    // Dependencies (The "Auto-Barrier" manifest)
    Mel_Gpu_Buffer**  read_buffers;
    Mel_Gpu_Buffer**  write_buffers;
    Mel_Gpu_Texture** read_textures;
     Mel_Gpu_Texture** write_textures;
} Mel_Gpu_Node_Desc;

void mel_render_graph_add_node(Mel_Render_Graph* g, Mel_Gpu_Node_Desc desc);
void mel_render_graph_execute(Mel_Render_Graph* g);

/*
 * MEL_STREAM (The Interchange)
 * ----------------------------
 * A specialized module that manages a persistent GPU buffer for CPU-to-GPU data.
 */

typedef struct {
    Mel_Gpu_Buffer* buffer;
    u8*             mapped_ptr;
    u32             cursor;
} Mel_Stream;

void* mel_stream_push(Mel_Stream* s, usize size);
