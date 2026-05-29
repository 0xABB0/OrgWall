// nob is a thin driver over the Melody build framework (tools/build). It
// rebuilds itself when any framework source changes, then hands off to
// mel_build_main, which discovers per-target build.c modules, loads them, and
// runs the build graph. The framework is split into a build library (build.c,
// the mel_build_*/mel_tp_* API archived into libmelbuild.a and statically
// linked into each target module) and the runner engine (runner.c). Both are
// compiled into this translation unit, so nob no longer needs to export its
// symbols to dynamically loaded modules.
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "clang", "-std=c23", "-g", "-o", binary_path, source_path

#include "tools/build/build.c"
#include "tools/build/runner.c"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv,
                               "tools/build/build.c", "tools/build/build.h",
                               "tools/build/internal.h", "tools/build/runner_internal.h",
                               "tools/build/runner.c",
                               "tools/build/runner_platform.c", "tools/build/runner_discovery.c",
                               "tools/build/runner_resolve.c", "tools/build/runner_compile.c",
                               "tools/build/runner_stages.c", "tools/build/runner_codegen.c",
                               "tools/build/runner_ninja.c",
                               "tools/build/runner_android.c",
                               "tools/build/runner_graph.c", "tools/build/runner_driver.c");
    return mel_build_main(argc, argv);
}
