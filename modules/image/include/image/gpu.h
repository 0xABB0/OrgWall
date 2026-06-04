#pragma once

#include <image/format.h>

#include <gpu/format.h>

Mel_Gpu_Format          mel_image_to_gpu_format(const mel_image_format* fmt);
const mel_image_format* mel_image_from_gpu_format(Mel_Gpu_Format fmt);
