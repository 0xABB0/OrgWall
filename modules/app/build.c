#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "app");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX)), "src/posix/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.c", "src/ios/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_whole_archive(lib, WHEN(.platforms = MEL_ON(ANDROID)));
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit");
    mel_depends(lib, "core");
    mel_depends(lib, "gui");
    mel_depends(lib, "reactor");
}
