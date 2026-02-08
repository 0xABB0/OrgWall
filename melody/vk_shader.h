#ifndef MEL_VK_SHADER_H
#define MEL_VK_SHADER_H

#include "vk_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    VkShaderModule vertex;
    VkShaderModule fragment;
} Mel_VkShader;

typedef struct
{
    const char* source;
    const char* vertex_entry;
    const char* fragment_entry;
} Mel_VkShader_Opt;

bool mel_vk_shader_init_opt(Mel_VkShader* shader, Mel_VkContext* ctx, Mel_VkShader_Opt opt);
#define mel_vk_shader_init(shader, ctx, ...) mel_vk_shader_init_opt((shader), (ctx), (Mel_VkShader_Opt){__VA_ARGS__})

void mel_vk_shader_shutdown(Mel_VkShader* shader, Mel_VkContext* ctx);

bool mel_slang_init(void);
void mel_slang_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
