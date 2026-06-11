#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-speech");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "gui");
    mel_depends(app, "speech");
    mel_depends(app, "future");
    mel_depends(app, "executor");
    mel_depends(app, "thread");
    mel_depends(app, "collection");
    mel_depends(app, "allocator");
    mel_depends(app, "string");
    mel_depends(app, "log");
    mel_depends(app, "core");

    mel_apple_plist(app, "apple/Info.plist.partial");

    mel_manifest(app, "APP_LABEL", "Speech");
    mel_manifest(app, "BUNDLE_ID", "orgwall.speech");
}
