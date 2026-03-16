#pragma once

#include "text.pass.fwd.h"
#include "core.types.h"
#include "event.channel.fwd.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "math.mat4.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.texture.h"
#include "gpu.buffer.h"
#include "gpu.cmd.h"
#include "render.list.fwd.h"
#include "render.pass.fwd.h"
#include "allocator.fwd.h"

#ifndef MEL_MAX_FRAMES_IN_FLIGHT
#define MEL_MAX_FRAMES_IN_FLIGHT 3
#endif

typedef enum {
    MEL_TEXT_RENDER_ATLAS = 0,
    MEL_TEXT_RENDER_SDF = 1,
    MEL_TEXT_RENDER_MSDF = 2,
} Mel_Text_Render_Mode;

typedef struct {
    Mel_Vec4 color;
    Mel_Vec4 outline_color;
    f32 edge;
    f32 softness;
    f32 outline;
    f32 px_range;
    u64 sort_key;
} Mel_Text_Style;

struct Mel_Text_Entry {
    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Rect uv;
    Mel_Vec4 color;
    Mel_Vec4 outline_color;
    Mel_Gpu_Texture* texture;
    f32 edge;
    f32 softness;
    f32 outline;
    f32 px_range;
    u32 mode;
};

typedef struct {
    VkDescriptorSet descriptor;
    u32 index_offset;
    u32 index_count;
} Mel_Text_Draw_Cmd;

typedef struct {
    Mel_Gpu_Texture* texture;
    VkDescriptorSet descriptor;
} Mel_Text_Descriptor_Cache_Entry;

typedef struct {
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;
} Mel_Text_Gpu_Frame;

struct Mel_Text_Pass {
    Mel_Gpu_Shader shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Texture white_texture;
    Mel_Gpu_Device* dev;

    Mel_Text_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 gpu_frame_index;
    void* vertices;
    u16* indices;
    u32 vertex_count;
    u32 index_count;
    u32 max_glyphs;

    Mel_Text_Draw_Cmd* draws;
    u32 draw_count;
    u32 draw_capacity;
    VkDescriptorSet current_descriptor;
    Mel_Gpu_Texture* current_texture;

    Mel_Text_Descriptor_Cache_Entry* descriptor_cache;
    u32 descriptor_cache_count;
    u32 descriptor_cache_capacity;

    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    VkFormat color_format;
    u32 max_glyphs;
    const Mel_Alloc* alloc;
} Mel_Text_Pass_Init_Opt;

bool mel_text_pass_init_opt(Mel_Text_Pass* pass, Mel_Text_Pass_Init_Opt opt);
#define mel_text_pass_init(pass, ...) mel_text_pass_init_opt((pass), (Mel_Text_Pass_Init_Opt){__VA_ARGS__})

void mel_text_pass_shutdown(Mel_Text_Pass* pass);
void mel_text_pass_execute(Mel_Render_Pass_Ctx* ctx);

static inline Mel_Text_Style mel_text_style(Mel_Vec4 color)
{
    return (Mel_Text_Style){
        .color = color,
        .outline_color = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f),
        .edge = 0.5f,
        .softness = 0.08f,
        .outline = 0.0f,
        .px_range = 4.0f,
        .sort_key = 0,
    };
}

extern Mel_Event_Channel mel_text_pass_ready;
