// nob is a thin driver over the Melody build framework (lib/build). It rebuilds
// itself when any framework source changes, then hands off to mel_build_main,
// which discovers per-target build.c modules, loads them, and runs the build
// graph. -rdynamic is required so the dlopen-ed target modules resolve the
// mel_build_* API against this binary.
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "clang", "-std=c23", "-g", "-rdynamic", "-o", binary_path, source_path

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_NO_ECHO
#include "nob.h"

#include "lib/build/build.c"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "lib/build/build.c", "lib/build/build.h");
    return mel_build_main(argc, argv);
}
