// nob is a thin driver over the Melody build framework (lib/build). It rebuilds
// itself when any framework source changes, then hands off to mel_build_main,
// which discovers per-target build.c modules, loads them, and runs the build
// graph. The mel_build_* API must be reachable from the dynamically loaded
// target modules: on ELF hosts -rdynamic exposes the binary's symbol table; on
// Windows we generate an import library (nob.lib) so the target module DLLs
// can link against it. MEL_BUILD_HOST gates __declspec(dllexport) annotations
// on the API declarations (a no-op on non-Windows).
#ifdef _WIN32
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "clang", "-std=c23", "-g", "-DMEL_BUILD_HOST", \
    "-o", binary_path, source_path, "-Wl,/IMPLIB:nob.lib"
#else
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "clang", "-std=c23", "-g", "-rdynamic", "-DMEL_BUILD_HOST", \
    "-o", binary_path, source_path
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_NO_ECHO
#include "nob.h"

#include "lib/build/build.c"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "lib/build/build.c", "lib/build/build.h");
    return mel_build_main(argc, argv);
}
