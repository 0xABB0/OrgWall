"""Host-aware provider of llvm-rc for mel_win32_resources.

The win32 resource compile needs llvm-rc. On a unix exec host (the macOS/Linux
cross build) it comes from the hermetic @llvm_toolchain_llvm distribution. On a
native Windows host that repo is empty (toolchains_llvm refuses to populate on
Windows), so llvm-rc is taken from the locally installed LLVM named by $BAZEL_LLVM.
A directory junction (no privilege required, unlike a file symlink) exposes the
local bin; only llvm-rc.exe is surfaced, so rc actions do not stage the whole
toolchain.
"""

def _impl(rctx):
    if rctx.os.name.startswith("windows"):
        llvm = rctx.os.environ.get("BAZEL_LLVM")
        if not llvm:
            fail("mel_win32_rc: BAZEL_LLVM must point at the local LLVM (providing bin/llvm-rc.exe) on a Windows host")
        rctx.symlink(llvm + "/bin", "bin")
        rctx.file("BUILD.bazel", "\n".join([
            'package(default_visibility = ["//visibility:public"])',
            'filegroup(name = "llvm-rc", srcs = ["bin/llvm-rc.exe"])',
            'filegroup(name = "bin", srcs = ["bin/llvm-rc.exe"])',
            "",
        ]))
    else:
        rctx.file("BUILD.bazel", "\n".join([
            'package(default_visibility = ["//visibility:public"])',
            'alias(name = "llvm-rc", actual = "@llvm_toolchain_llvm//:bin/llvm-rc")',
            'alias(name = "bin", actual = "@llvm_toolchain_llvm//:bin")',
            "",
        ]))

mel_win32_rc = repository_rule(
    implementation = _impl,
    environ = ["BAZEL_LLVM"],
    local = True,
)
