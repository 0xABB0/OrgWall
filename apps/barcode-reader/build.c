#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "barcode-reader");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "camera");
    mel_depends(app, "barcode");
    mel_depends(app, "image");
    mel_depends(app, "reactor");
    mel_depends(app, "executor");
    mel_depends(app, "allocator");
    mel_depends(app, "string");
    mel_depends(app, "log");
    mel_depends(app, "core");
}
