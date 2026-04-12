#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <stb_image_write.h>
#pragma clang diagnostic pop

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
