#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "camera-gui");
    mel_subsystem(app, "gui");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_manifest(app, "APP_LABEL", "Camera Bench");
    mel_manifest(app, "BUNDLE_ID", "orgwall.camera-gui");
    mel_apple_plist(app, "apple/Info.plist.partial");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "gui");
    mel_depends(app, "camera");
    mel_depends(app, "image");
    mel_depends(app, "paint");
    mel_depends(app, "collection");
    mel_depends(app, "time");
    mel_depends(app, "thread");
    mel_depends(app, "future");
    mel_depends(app, "executor");
    mel_depends(app, "allocator");
    mel_depends(app, "color");
    mel_depends(app, "string");
    mel_depends(app, "log");
    mel_depends(app, "core");
}
