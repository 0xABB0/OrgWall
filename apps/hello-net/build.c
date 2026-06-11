#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "hello-net");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_unavailable(app, WHEN(.platforms = MEL_ON(WASM)));
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "net");
    mel_depends(app, "http");
    mel_depends(app, "io");
    mel_depends(app, "port");
    mel_depends(app, "future");
    mel_depends(app, "executor");
    mel_depends(app, "collection");
    mel_depends(app, "string");
    mel_depends(app, "allocator");
    mel_depends(app, "time");
    mel_depends(app, "log");
    mel_depends(app, "core");

    mel_manifest(app, "APP_LABEL", "Hello Net");
    mel_manifest(app, "BUNDLE_ID", "orgwall.hellonet");
}
