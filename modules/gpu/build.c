#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "gpu");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.gpu = "metal"), "src/metal/*.m");
    mel_sources(lib, WHEN(.gpu = "vulkan"), "src/vulkan/*.c");
    mel_sources(lib, WHEN(.gpu = "webgpu"), "src/webgpu/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "metal"), "-framework", "Metal", "-framework", "QuartzCore",
             "-framework", "Foundation");
    mel_depends(lib, "core");
    mel_depends(lib, "reactor");
    mel_depends(lib, "time");
}
