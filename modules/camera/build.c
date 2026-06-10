#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "camera");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/camera.c");
    mel_sources(lib, ALWAYS, "src/descriptors.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_android_manifest(lib, "src/android/AndroidManifest.xml");
    mel_android_java(lib, "src/android/java");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "AVFoundation", "-framework", "CoreMedia", "-framework", "CoreVideo", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-lcamera2ndk", "-lmediandk", "-landroid", "-llog");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lmfplat", "-lmf", "-lmfreadwrite", "-lmfuuid", "-lole32");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "image");
    mel_depends(lib, "event");
    mel_depends(lib, "future");
    mel_depends(lib, "executor");
    mel_depends(lib, "string");
    mel_depends(lib, "log");
    mel_depends_when(lib, "platform", WHEN(.platforms = MEL_ON(ANDROID)));

    Mel_Target* t = mel_add_test(b, "camera-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/camera.c");
    mel_sources(t, ALWAYS, "src/descriptors.c");
    mel_sources(t, ALWAYS, "test/camera_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "image");
    mel_depends(t, "event");
    mel_depends(t, "future");
    mel_depends(t, "executor");
    mel_depends(t, "string");
    mel_depends(t, "log");
}
