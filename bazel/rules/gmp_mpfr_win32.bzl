"""Native-Windows-host build of gmp + mpfr, mirroring the old nob recipe.

rules_foreign_cc's configure_make cannot build gmp on a Windows host: it drives
the registered cc_toolchain (clang-cl, MSVC CLI flags) through gmp's autotools
configure, which only understands a GNU-style cc, and it bootstraps its own make
via WSL bash. nob sidestepped all of that by running `sh configure && make &&
make install` directly with the plain `clang` driver (MSVC ABI), MSYS2 sh/make,
and the llvm-* binutils, then mirroring lib<x>.a -> <x>.lib for the MSVC linker.

This repository rule reproduces that, in a real directory with the host PATH (the
nob conditions), so it only runs when the native build actually needs it (the
gmp/mpfr targets route here only under //bazel/config:native_win32). The macOS
cross path keeps using configure_make untouched.

Paths handed to the native clang are kept Windows-style (D:/...), not cygwin
(/d/...), so both native clang and the MSYS2 tools accept them (nob's trick).
"""

_BUILD_FILE = """\
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "gmp",
    srcs = ["prefix/lib/gmp.lib"],
    hdrs = ["prefix/include/gmp.h"],
    includes = ["prefix/include"],
)

cc_library(
    name = "mpfr",
    srcs = ["prefix/lib/mpfr.lib"],
    hdrs = [
        "prefix/include/mpfr.h",
        "prefix/include/mpf2mpfr.h",
    ],
    includes = ["prefix/include"],
    deps = [":gmp"],
)
"""

def _win(path):
    return str(path).replace("\\", "/")

def _impl(rctx):
    if not rctx.os.name.startswith("windows"):
        fail("gmp_mpfr_win32 is for a native Windows host only; the cross build uses configure_make")

    msys_bin = "C:/tools/msys64/usr/bin"
    bash = rctx.path(msys_bin + "/bash.exe")
    if not bash.exists:
        fail("gmp_mpfr_win32: MSYS2 bash not found at %s (needs sh/make/m4/perl)" % bash)

    llvm = rctx.os.environ.get("BAZEL_LLVM")
    if not llvm:
        fail("gmp_mpfr_win32: BAZEL_LLVM must point at the local LLVM (the clang driver)")
    llvm_bin = _win(rctx.path(llvm)) + "/bin"

    gmp_src = _win(rctx.path(Label("//third-party/gmp:BUILD.bazel")).dirname) + "/gmp"
    mpfr_src = _win(rctx.path(Label("//third-party/mpfr:BUILD.bazel")).dirname) + "/mpfr"
    repo = _win(rctx.path(""))
    prefix = repo + "/prefix"

    # Windows ;-separated PATH for the process spawn (cygwin DLLs + clang); the
    # script re-exports a cygwin :-separated PATH for the autotools run itself.
    win_path = msys_bin + ";" + llvm_bin + ";" + rctx.os.environ.get("PATH", "")

    # Copy the pristine source trees in and build in-place (srcdir = '.'): an
    # absolute D:/ srcdir breaks gmp's aux-file detection (per nob).
    for src, dst in [(gmp_src, "gmp"), (mpfr_src, "mpfr")]:
        res = rctx.execute([msys_bin + "/cp.exe", "-rp", src, dst], environment = {"PATH": win_path}, timeout = 600)
        if res.return_code != 0:
            fail("gmp_mpfr_win32: copy %s failed:\n%s\n%s" % (src, res.stdout, res.stderr))

    # Neutralize autotools maintainer-mode: skewed mtimes (git checkout + copy) make
    # `make` try to regenerate shipped aclocal.m4/configure/*.info via aclocal/automake/
    # makeinfo, none of which are installed. Pointing each at `true` is a no-op, so the
    # prebuilt generated files are used as-is.
    noregen = "MAKEINFO=true ACLOCAL=true AUTOMAKE=true AUTOCONF=true AUTOHEADER=true"
    cfg = "CC='clang -std=gnu17' AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm LD=ld.lld " + noregen
    script = "\n".join([
        "#!/bin/bash",
        "set -e",
        # cygwin-style PATH so configure finds sh/make/m4 and the clang driver.
        'export PATH="/c/tools/msys64/usr/bin:' + llvm_bin + ':$PATH"',
        'PREFIX="' + prefix + '"',
        'mkdir -p "$PREFIX"',
        # clang MSVC -l<x>/linker wants <x>.lib; gmp/mpfr emit lib<x>.a. Mirror after
        # each install so the next configure's link test (mpfr needs -lgmp) resolves.
        'mirror() { for a in "$PREFIX"/lib/lib*.a; do [ -e "$a" ] || continue; b=$(basename "$a" .a); cp -f "$a" "$PREFIX/lib/${b#lib}.lib"; done; }',
        "cd gmp",
        "./configure --prefix=\"$PREFIX\" --disable-shared --enable-static --disable-assembly " + cfg,
        "make -j8",
        "make install",
        "mirror",
        "cd ..",
        "cd mpfr",
        "./configure --prefix=\"$PREFIX\" --disable-shared --enable-static --with-gmp=\"$PREFIX\" " + cfg,
        "make -j8",
        "make install",
        "mirror",
        "cd ..",
        "",
    ])
    rctx.file("build.sh", script, executable = True)

    res = rctx.execute([bash, "build.sh"], environment = {"PATH": win_path}, timeout = 1800, quiet = False)
    if res.return_code != 0:
        fail("gmp_mpfr_win32: build failed (rc=%d)\n--- stdout ---\n%s\n--- stderr ---\n%s" % (res.return_code, res.stdout, res.stderr))

    rctx.file("BUILD.bazel", _BUILD_FILE, executable = False)

gmp_mpfr_win32 = repository_rule(
    implementation = _impl,
    environ = ["BAZEL_LLVM"],
    local = True,
)
