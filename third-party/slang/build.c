#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "slang");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_includes(lib, MEL_PRIVATE, ALWAYS, "dist/include");
    mel_sources(lib, ALWAYS, "src/*.cpp");
    // Link the vendored libslang + the C++ runtime. macOS/iOS/Linux need an explicit libc++;
    // Windows (clang-msvc) links the C++ runtime automatically.
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)),
             "-Lthird-party/slang/dist/lib", "-lslang", "-lc++", "-Wl,-rpath,third-party/slang/dist/lib");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)),
             "-Lthird-party/slang/dist/lib", "-lslang", "-lc++", "-Wl,-rpath,$ORIGIN");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)),
             "-Lthird-party/slang/dist/lib", "-lslang");

    Mel_Target* t = mel_add_test(b, "slang-compile");
    mel_sources(t, ALWAYS, "test/compile_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "slang");
    mel_depends(t, "test");
    mel_depends(t, "core");
}
