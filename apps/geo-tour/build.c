#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "geo-tour");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_depends(app, "boot");
    mel_depends(app, "vat");
    mel_depends(app, "geolocation");
    mel_depends(app, "future");
    mel_depends(app, "executor");
    mel_depends(app, "allocator");
    mel_depends(app, "collection");
    mel_depends(app, "string");
    mel_depends(app, "log");
    mel_depends(app, "core");

    mel_manifest(app, "APP_LABEL", "Geo Tour");
    mel_manifest(app, "BUNDLE_ID", "orgwall.geotour");
    mel_apple_plist(app, "apple/Info.plist.partial");
}
