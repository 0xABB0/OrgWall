#pragma once

#include "gpu.types.h"
#include "gpu.device.fwd.h"
#include "gpu.cmd.fwd.h"
#include "allocator.fwd.h"

typedef struct Mel_Gpu_Image_State Mel_Gpu_Image_State;
typedef struct Mel_Gpu_Image Mel_Gpu_Image;

struct Mel_Gpu_Image_State {
    Mel_Gpu_Image_Layout layout;
    Mel_Gpu_Stage stage;
    Mel_Gpu_Access access;
};

struct Mel_Gpu_Image {
    void* _handle;
    void* _view;
    void* _allocation;
    Mel_Gpu_Format format;
    u32 width;
    u32 height;
    u32 mip_levels;
    u32 layer_count;
    Mel_Gpu_Aspect aspect;
    Mel_Gpu_Image_State* subresource_states;
    const Mel_Alloc* alloc;
};

typedef struct {
    u32 width;
    u32 height;
    Mel_Gpu_Format format;
    Mel_Gpu_Image_Usage usage;
    Mel_Gpu_Aspect aspect;
    u32 mip_levels;
    u32 layer_count;
    const Mel_Alloc* alloc;
} Mel_Gpu_Image_Opt;

void mel_gpu_image_init_opt(Mel_Gpu_Image* img, Mel_Gpu_Device* dev, Mel_Gpu_Image_Opt opt);
#define mel_gpu_image_init(img, dev, ...) mel_gpu_image_init_opt((img), (dev), (Mel_Gpu_Image_Opt){__VA_ARGS__})

void mel_gpu_image_shutdown(Mel_Gpu_Image* img, Mel_Gpu_Device* dev);

void mel_gpu_image_transition(Mel_Gpu_Image* img, Mel_Gpu_Cmd* cmd,
                              Mel_Gpu_Image_Layout new_layout);

void mel_gpu_image_transition_subresource(Mel_Gpu_Image* img, Mel_Gpu_Cmd* cmd,
                                          u32 mip, u32 layer, Mel_Gpu_Image_Layout new_layout);

Mel_Gpu_Image_State mel_gpu_image_state(Mel_Gpu_Image* img, u32 mip, u32 layer);

Mel_Gpu_Stage mel_gpu_image_layout_stage(Mel_Gpu_Image_Layout layout);
Mel_Gpu_Access mel_gpu_image_layout_access(Mel_Gpu_Image_Layout layout);
