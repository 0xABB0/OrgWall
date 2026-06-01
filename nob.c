#define NOB_REBUILD_URSELF(binary_path, source_path) "clang", "-std=c23", "-g", "-Imodules/build", "-o", binary_path, source_path

#define NOB_IMPLEMENTATION
#include "nob.h"

#include "modules/build/api.c"
#include "modules/build/util.c"
#include "modules/build/select.c"
#include "modules/build/discovery.c"
#include "modules/build/graph.c"
#include "modules/build/resolve.c"
#include "modules/build/toolchain.c"
#include "modules/build/thirdparty.c"
#include "modules/build/package.c"
#include "modules/build/emit.c"
#include "modules/build/compdb.c"
#include "modules/build/driver.c"

int main(int argc, char** argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "nob.c", "modules/build/build.h", "modules/build/internal.h", "modules/build/runner.h", "modules/build/api.c", "modules/build/util.c",
                               "modules/build/select.c", "modules/build/discovery.c", "modules/build/graph.c", "modules/build/resolve.c", "modules/build/toolchain.c", "modules/build/thirdparty.c",
                               "modules/build/package.c", "modules/build/emit.c", "modules/build/compdb.c", "modules/build/driver.c");
    return mel_build_main(argc, argv);
}
