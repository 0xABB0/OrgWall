#include "build.h"

void build(Mel_Build* b)
{
    // All-dynamic, first-class integration: one shared libLLVM.dylib linked by both our ORC
    // (llvm module) and clang (clang module, incl. clang::Interpreter) -> a single LLVM image
    // with a single TargetRegistry. libclang-cpp.dylib links libLLVM.dylib (@rpath), so there is
    // exactly one LLVM copy; the C API (target-init, ORC) is exported, and being dynamic the
    // global constructors run self-consistently at load (no static-init / libc++ TMO hazard).
    //
    // macOS source: Homebrew LLVM (`brew install llvm`). Linux: distro libLLVM.so + libclang-cpp.so.
    // Windows: LLVM installer LLVM-C.dll + libclang. Path discovery (llvm-config / brew --prefix)
    // is a follow-up; the macOS prefix is pinned here.
    Mel_Target* rt = mel_add_third_party(b, "llvm-runtime");

    mel_includes(rt, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "/opt/homebrew/opt/llvm/include");
    mel_link(rt, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)),
             "-L/opt/homebrew/opt/llvm/lib", "-Wl,-rpath,/opt/homebrew/opt/llvm/lib",
             "-lLLVM", "-lclang-cpp");
}
