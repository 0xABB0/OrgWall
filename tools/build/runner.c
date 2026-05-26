// Melody build runner (engine).
//
// The build graph driver: source discovery, the content-addressed cache, the
// default configure/compile/link/package stages, the Android NDK pipeline, and
// target discovery + module loading. Part of the nob driver translation unit
// (nob.c includes build.c, then this file); it reuses the static helpers and
// the public API defined in build.c. Per-target build.c modules statically link
// libmelbuild.a (built from build.c) and are dlopen'd here purely for project().

#include "runner_internal.h"

#include "runner_platform.c"
#include "runner_discovery.c"
#include "runner_resolve.c"
#include "runner_compile.c"
#include "runner_stages.c"
#include "runner_android.c"
#include "runner_graph.c"
#include "runner_driver.c"
