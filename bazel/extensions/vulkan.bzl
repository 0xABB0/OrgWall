"""Host-discovered macOS Vulkan loader as @vulkan_macos.

macOS ships no vendored Vulkan runtime; the loader (libvulkan.dylib fronting the
MoltenVK ICD) arrives via the LunarG SDK or Homebrew. Its location is discovered,
not hardcoded — the principle this repo already applies to the NDK
($ANDROID_NDK_HOME) and the macOS SDK (xcrun, in //bazel/extensions:llvm.bzl).

$VULKAN_SDK (LunarG; its setup-env points at .../macOS) wins if exported;
otherwise `brew --prefix` locates the Homebrew cellar. Headers are NOT taken from
here — dependents use the vendored //third-party/vulkan-headers — so only the
loader's link directory is emitted.

The runtime dependency on a host-present libvulkan.dylib is irreducible, exactly
as the macOS SDK isysroot is. This rule removes the hand-written /opt/homebrew
path; it does not vendor the dylib. The further, fully input-tracked step is to
commit libvulkan.dylib and cc_import it (mirroring //third-party/alsa).
"""

_BUILD = """\
cc_library(
    name = "loader",
    linkopts = ["-L{libdir}", "-lvulkan"],
    visibility = ["//visibility:public"],
)
"""

def _impl(rctx):
    prefix = rctx.os.environ.get("VULKAN_SDK")
    if not prefix:
        res = rctx.execute(["brew", "--prefix"])
        if res.return_code != 0:
            fail("@vulkan_macos: $VULKAN_SDK unset and `brew --prefix` failed (%s); export VULKAN_SDK or `brew install vulkan-loader`." % res.stderr.strip())
        prefix = res.stdout.strip()
    libdir = prefix + "/lib"
    if not rctx.path(libdir + "/libvulkan.dylib").exists:
        fail("@vulkan_macos: libvulkan.dylib not found in %s; install the Vulkan loader (LunarG SDK or `brew install vulkan-loader`)." % libdir)
    rctx.file("BUILD.bazel", _BUILD.format(libdir = libdir))

vulkan_sysroot = repository_rule(
    implementation = _impl,
    environ = ["VULKAN_SDK"],
    configure = True,
    local = True,
)
