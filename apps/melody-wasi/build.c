#include "build.h"

// A non-GUI compute target that exercises the wasi-sdk toolchain end to end:
// melody (its web-supporting subset) compiled to a standalone wasm32-wasip1
// module, runnable under any wasi host (wasmtime, node:wasi). wasi has no DOM,
// so this is the compute counterpart to the emscripten GUI build.
bool project(Mel_Build_Target *t)
{
    mel_build_set_name(t, "melody-wasi");
    mel_build_set_kind(t, MEL_TARGET_APPLICATION);

    static const Mel_Platform web_only[] = { MEL_PLATFORM_WEB };
    mel_build_set_platforms(t, web_only, 1);
    mel_build_use_runtime_on(t, MEL_PLATFORM_WEB, "wasi");

    mel_build_add_source_root(t, "apps/melody-wasi/src");
    mel_build_add_dependency(t, "melody");
    return true;
}
