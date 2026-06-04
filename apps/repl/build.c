#include "build.h"

void build(Mel_Build* b)
{
    // Native C REPL: stdin -> clang frontend -> llvm ORC JIT -> evaluated live.
    // Native hosts only; wasm cannot codegen in-browser (design/jit-compiler.md).
    Mel_Target* app = mel_add_executable(b, "repl-cli");
    mel_sources(app, ALWAYS, "src/*.c");
    mel_unavailable(app, WHEN(.platforms = MEL_ON(WASM)));
    mel_depends(app, "repl");
    mel_depends(app, "clang");
    mel_depends(app, "llvm");
    mel_depends(app, "jit");
    mel_depends(app, "core");
    mel_depends(app, "allocator");
    mel_depends(app, "string");
    mel_depends(app, "collection");
    mel_depends(app, "log");
}
