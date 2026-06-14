cc_import(
    name = "slang_compiler_import",
    interface_library = "lib/slang-compiler.lib",
    shared_library = "bin/slang-compiler.dll",
)

cc_library(
    name = "slang_runtime",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [":slang_compiler_import"],
)
