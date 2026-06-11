#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "notification");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/notification.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "apple/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "linux/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "web/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/notification_host_none.c");

    mel_cflags(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-fobjc-arc");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Foundation", "-framework", "UserNotifications", "-framework", "AppKit");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "Foundation", "-framework", "UserNotifications", "-framework", "UIKit");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-ldbus-1", "-ldl");

    mel_android_java(lib, "android/java");
    mel_android_manifest(lib, "android/AndroidManifest.xml");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "log");
    mel_depends_when(lib, "platform", WHEN(.platforms = MEL_ON(ANDROID)));

    Mel_Target* t = mel_add_test(b, "notification-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/notification_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_cflags(t, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS)), "-fobjc-arc");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Foundation", "-framework", "UserNotifications", "-framework", "AppKit");
    mel_depends(t, "test");
    mel_depends(t, "notification");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "log");
}
