"""Pinned, hermetic LLVM as @llvm.

libclang (C API) for codegen host tools; libclang-cpp for the ORC JIT (llvm
module) and clang::Interpreter (clang module). The official LLVM release is
sha256-pinned and downloaded — a pinned prebuilt is hermetic by Bazel's own
definition; we do NOT build LLVM from source (a multi-hour, ~10 GB-RAM-per-link
build that rules_foreign_cc rebuilds whole on every cache miss).

The macOS SDK isysroot still comes from host Xcode via xcrun. That is an
irreducible host dependency — the same one every iOS build carries — not a
hermeticity regression within our control.

Version is pinned to match the toolchain the code was written against. To add a
platform, pin its release asset's sha256 alongside the macOS one and branch on
rctx.os in _llvm_repo_impl.
"""

_VERSION = "22.1.6"

_MACOS_ARM64 = struct(
    url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-{v}/LLVM-{v}-macOS-ARM64.tar.xz".format(v = _VERSION),
    sha256 = "8059d9d9eeb059c30d812b4a37291888f8dcba04d2b5ace61fd12d2904eaa0e9",
    strip_prefix = "LLVM-{v}-macOS-ARM64".format(v = _VERSION),
    ext = "dylib",
)

# The official release ships NO shared libLLVM (LLVM proper is 211 static
# archives); the two shared libs are self-contained. libclang.dylib embeds the
# LLVM it needs; libclang-cpp.dylib exports clang::Interpreter AND llvm::orc::*
# (LLJIT/ExecutionSession), so it alone satisfies the JIT/REPL runtime.
_BUILD = """\
cc_library(
    name = "libclang",
    srcs = ["lib/libclang.{ext}"],
    hdrs = glob(["include/clang-c/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "llvm_runtime",
    # clang::Interpreter + llvm::orc come from libclang-cpp.dylib. KNOWN LIMIT:
    # the release also ships LLVM as LTO-bitcode static archives that Apple ld64
    # (libLTO 17) cannot parse, and the dylib hides LLVMInitialize*/some RTTI — so
    # apps/repl does NOT fully link under the Apple toolchain. That is resolved by
    # the upstream LLVM/lld toolchain, the same one the C++26/reflection move
    # requires; deferred to that toolchain decision, not papered over here.
    srcs = ["lib/libclang-cpp.{ext}"],
    textual_hdrs = glob(["include/**"], allow_empty = True),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
"""

def _llvm_repo_impl(rctx):
    a = _MACOS_ARM64
    rctx.download_and_extract(
        url = a.url,
        sha256 = a.sha256,
        stripPrefix = a.strip_prefix,
    )
    rctx.file("BUILD.bazel", _BUILD.format(ext = a.ext))

    sdk = rctx.execute(["xcrun", "--show-sdk-path"]).stdout.strip()
    # clang builtin headers (stdbool.h, stddef.h, ...) live in the version-stamped
    # resource dir; libclang needs them on its isystem path to parse TUs. Read un-
    # sandboxed at action time (the mel_codegen action runs `local`).
    res = ""
    clang_root = rctx.path("lib/clang")
    if clang_root.exists:
        kids = clang_root.readdir()
        if kids:
            res = str(kids[0]) + "/include"
    rctx.file("sdk.bzl", 'MACOS_SDK = "%s"\nLLVM_RESOURCE_INCLUDE = "%s"\n' % (sdk, res))

_llvm_repo = repository_rule(
    implementation = _llvm_repo_impl,
)

def _llvm_ext_impl(_mctx):
    _llvm_repo(name = "llvm")

llvm_ext = module_extension(implementation = _llvm_ext_impl)
