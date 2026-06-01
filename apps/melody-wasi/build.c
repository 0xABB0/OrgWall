#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "melody-wasi");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "allocator");
    mel_depends(app, "collection");
    mel_depends(app, "core");
    mel_depends(app, "string");
}
