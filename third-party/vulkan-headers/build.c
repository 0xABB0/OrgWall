#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* tp = mel_add_third_party(b, "vulkan-headers");
    mel_includes(tp, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(LINUX)), "include");
}
