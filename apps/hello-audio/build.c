#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-audio");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "audio");
    mel_depends(app, "core");
    mel_depends(app, "allocator");
    mel_depends(app, "reactor");
    mel_depends(app, "executor");
    mel_depends(app, "time");
    mel_depends(app, "thread");
    mel_depends(app, "math");
    mel_depends(app, "log");
}
