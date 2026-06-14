"""Hermetic MSVC-ABI Windows sysroot via xwin.

Downloads the Microsoft CRT + Windows SDK from Microsoft's CDN (you accept MS's
license through xwin's --accept-license) and splats them. The SDK is never
redistributed — each host fetches it itself, honoring the license.

xwin ships no macOS prebuilt; on a macOS exec host install it once (cargo install
xwin, or brew install xwin) so it is on PATH or in ~/.cargo/bin. On linux/windows
exec hosts the prebuilt is fetched automatically. This repo is fetched lazily —
only a win32 build triggers the splat (no splat on macOS/Linux builds).

The MS "Windows Kits" dir is renamed to "WindowsKits": Bazel labels/globs cannot
hold the space. clang-cl is then pointed at the include/lib dirs explicitly via
/imsvc and /LIBPATH (its /winsysroot auto-detection is broken when cross-compiling
from a non-Windows host)."""

_XWIN_VERSION = "0.9.0"
_PREBUILT = {
    "linux-x86_64": ("x86_64-unknown-linux-musl", "tar.gz", "31e1033f30608ba6b821d17f1461042bd54c23424813c9b4e9ae15b6d32fa4cd"),
    "linux-aarch64": ("aarch64-unknown-linux-musl", "tar.gz", "41466ca41e16fe7fc1b82a67babc7c3811021bf32de354b90b34d8c4edb153e2"),
    "windows-x86_64": ("x86_64-pc-windows-msvc", "tar.gz", "36a03b1bc21ead290eda3891b5ddfe3219eed45dd592329412248d143a26dda2"),
}

def _xwin_bin(rctx):
    found = rctx.which("xwin")
    if found:
        return str(found)
    home = rctx.os.environ.get("HOME", "")
    if home:
        cand = rctx.path(home + "/.cargo/bin/xwin")
        if cand.exists:
            return str(cand)
    osn = rctx.os.name.lower()
    arch = rctx.os.arch.lower()
    a = "aarch64" if ("arm" in arch or "aarch" in arch) else "x86_64"
    key = ("linux-" + a) if "linux" in osn else ("windows-x86_64" if "win" in osn else None)
    if not key:
        fail("xwin not found and no prebuilt for host (os=%s arch=%s). Install it once: cargo install xwin (or brew install xwin)." % (osn, arch))
    triple, ext, sha256 = _PREBUILT[key]
    base = "xwin-%s-%s" % (_XWIN_VERSION, triple)
    rctx.download_and_extract(
        url = "https://github.com/Jake-Shadle/xwin/releases/download/%s/%s.%s" % (_XWIN_VERSION, base, ext),
        sha256 = sha256,
        stripPrefix = base,
    )
    return str(rctx.path("xwin.exe" if "win" in osn else "xwin"))

def _impl(rctx):
    xwin = _xwin_bin(rctx)
    res = rctx.execute(
        [
            xwin,
            "--accept-license",
            "--arch",
            "x86_64",
            "--variant",
            "desktop",
            "splat",
            "--use-winsysroot-style",
            "--preserve-ms-arch-notation",
            "--output",
            "winsysroot",
        ],
        timeout = 1800,
        environment = {"XWIN_ACCEPT_LICENSE": "1"},
    )
    if res.return_code != 0:
        fail("xwin splat failed (%d):\n%s\n%s" % (res.return_code, res.stdout, res.stderr))

    # `mv` ties the splat to a unix exec host (mac/linux); a Windows exec host is
    # unsupported, matching the cc_toolchain_config.
    mv = rctx.execute(["mv", "winsysroot/Windows Kits", "winsysroot/WindowsKits"])
    if mv.return_code != 0:
        fail("renaming 'Windows Kits' -> 'WindowsKits' failed: " + mv.stderr)

    # Marker inside winsysroot/: its execpath dirname gives the sysroot root to the
    # toolchain rule, so no load() of this repo (which would force an eager splat).
    rctx.file("winsysroot/SYSROOT_ROOT", "")
    rctx.file(
        "BUILD.bazel",
        'filegroup(name = "sysroot_files", srcs = glob(["winsysroot/**"], allow_empty = True), visibility = ["//visibility:public"])\n' +
        'exports_files(["winsysroot/SYSROOT_ROOT"])\n',
    )

xwin_sysroot = repository_rule(
    implementation = _impl,
    environ = ["HOME", "PATH", "XWIN_ACCEPT_LICENSE"],
)
