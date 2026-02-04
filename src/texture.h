#ifndef MEL_ASSETS_TEXTURE_H
#define MEL_ASSETS_TEXTURE_H

#include "vk_texture.h"
#include "vk_pipeline.h"

bool mel_texture_load(Mel_VkTexture* tex, Mel_VkContext* ctx, const char* path);
bool mel_texture_load_and_bind(Mel_VkTexture* tex, Mel_VkContext* ctx, Mel_VkPipeline* pipeline, const char* path);

#endif
