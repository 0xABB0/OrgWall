// Third-party library bootstrapping: gmp, mpfr, sdl3, sqlite3.
// Included by nob.c — relies on nob.h, PLATFORM_WIN32, and Target being
// defined before this file.

#ifndef THIRD_PARTY_DIR
#define THIRD_PARTY_DIR    "third-party"
#endif
#ifndef TP_BUILD_DIR
#define TP_BUILD_DIR       "build/third-party"
#endif

#if PLATFORM_WIN32
// Autotools' configure scripts reject paths containing backslashes (unsafe srcdir).
// Keep Windows drive paths intact and only normalize slashes. MSYS tools accept
// "D:/foo/bar", and Windows-target compilers can still open those paths.
static const char *to_autotools_path(const char *p) {
    if (!p) return p;
    size_t len = strlen(p);
    char *out = (char*)temp_alloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        out[i] = (p[i] == '\\') ? '/' : p[i];
    }
    out[len] = 0;
    return out;
}

// GNU make on Windows resolves $(SHELL) by searching PATH for sh.exe and
// substitutes the resolved path verbatim into recipes. Git for Windows ships
// sh at "C:\Program Files\Git\usr\bin\sh.exe" -- the embedded space then
// crashes libtool recipes like `$(SHELL) ../libtool ...`. Prepend the 8.3
// short form of the chosen sh's directory so make picks a path with no spaces.
static void win32_ensure_no_space_sh_in_path(void) {
    static bool done = false;
    if (done) return;
    done = true;

    char sh_full[MAX_PATH];
    DWORD n = SearchPathA(NULL, "sh.exe", NULL, MAX_PATH, sh_full, NULL);
    if (n == 0 || n >= MAX_PATH) return;

    bool has_space = false;
    for (const char *p = sh_full; *p; p++) if (*p == ' ') { has_space = true; break; }
    if (!has_space) return;

    char *sep = strrchr(sh_full, '\\');
    if (!sep) sep = strrchr(sh_full, '/');
    if (!sep) return;
    *sep = '\0';

    char short_dir[MAX_PATH];
    if (GetShortPathNameA(sh_full, short_dir, MAX_PATH) == 0) return;

    const char *old_path = getenv("PATH");
    if (!old_path) old_path = "";
    size_t need = strlen(short_dir) + 1 + strlen(old_path) + 1;
    char *new_path = (char*)temp_alloc(need);
    snprintf(new_path, need, "%s;%s", short_dir, old_path);
    _putenv_s("PATH", new_path);
}
#else
static const char *to_autotools_path(const char *p) { return p; }
static void win32_ensure_no_space_sh_in_path(void) {}
#endif

// --- Target helpers ---

static const char *target_prefix_rel(const Target *t) {
    return temp_sprintf("%s/%s", TP_BUILD_DIR, t->name);
}

static const char *target_prefix_abs(const Target *t) {
    return temp_sprintf("%s/%s", get_current_dir_temp(), target_prefix_rel(t));
}

static const char *target_include(const Target *t) {
    return temp_sprintf("%s/include", target_prefix_rel(t));
}

static const char *target_lib(const Target *t) {
    return temp_sprintf("%s/lib", target_prefix_rel(t));
}

static bool target_uses_msvc_lib_names(const Target *t) {
    return PLATFORM_WIN32 && t->configure_host == NULL;
}

static const char *autotools_static_lib(const Target *t, const char *name) {
    const char *prefix = target_prefix_rel(t);
    if (target_uses_msvc_lib_names(t)) {
        return temp_sprintf("%s/lib/%s.lib", prefix, name);
    }
    return temp_sprintf("%s/lib/lib%s.a", prefix, name);
}

static bool autotools_build(const Target *t, const char *src_rel, const char *name, const char *extra_arg) {
    win32_ensure_no_space_sh_in_path();

    const char *cwd = get_current_dir_temp();
    const char *abs_prefix = target_prefix_abs(t);
    const char *build_dir_rel = temp_sprintf("%s/%s-build", target_prefix_rel(t), name);
    const char *abs_build = temp_sprintf("%s/%s", cwd, build_dir_rel);
    const char *configure = temp_sprintf("../../../../%s/configure", src_rel);

    if (!mkdir_if_not_exists(build_dir_rel)) return false;
    if (!set_current_dir(abs_build)) return false;

    bool ok = true;

    Cmd cmd = {0};
#if PLATFORM_WIN32
    cmd_append(&cmd, "sh");
#endif
    cmd_append(&cmd, to_autotools_path(configure));
    cmd_append(&cmd, temp_sprintf("--prefix=%s", to_autotools_path(abs_prefix)));
    cmd_append(&cmd, "--disable-shared", "--enable-static", "--with-pic", "--disable-maintainer-mode");
    if (t->configure_host) cmd_append(&cmd, temp_sprintf("--host=%s", t->configure_host));
    if (extra_arg) cmd_append(&cmd, extra_arg);
    if (t->cc)     cmd_append(&cmd, temp_sprintf("CC=%s",     t->cc));
    if (t->ar)     cmd_append(&cmd, temp_sprintf("AR=%s",     t->ar));
    if (t->ranlib) cmd_append(&cmd, temp_sprintf("RANLIB=%s", t->ranlib));
#if PLATFORM_WIN32
    // VS LLVM ships ld.lld / llvm-nm / llvm-strip but no GNU-named aliases.
    // Autotools/libtool probe for "ld" / "nm" / "strip" by name and bail otherwise.
    cmd_append(&cmd, "LD=ld.lld");
    cmd_append(&cmd, "NM=llvm-nm");
    cmd_append(&cmd, "STRIP=llvm-strip");
#endif
    if (!cmd_run_sync_and_reset(&cmd)) ok = false;

    if (ok) {
        Cmd make = {0};
        cmd_append(&make, "make", temp_sprintf("-j%d", nob_nprocs()));
        if (!cmd_run_sync_and_reset(&make)) ok = false;
    }

    if (ok) {
        Cmd install = {0};
        cmd_append(&install, "make", "install");
        if (!cmd_run_sync_and_reset(&install)) ok = false;
    }

    set_current_dir(cwd);
    return ok;
}

static bool single_tu_build(const Target *t, const char *src_rel, const char *name,
                             const char *const *headers, size_t headers_count,
                             const char *const *extra_cflags, size_t extra_cflags_count) {
    const char *cwd = get_current_dir_temp();
    const char *abs_src = temp_sprintf("%s/%s", cwd, src_rel);
    const char *abs_prefix = target_prefix_abs(t);
    const char *build_dir_rel = temp_sprintf("%s/%s-build", target_prefix_rel(t), name);
    if (!mkdir_if_not_exists(build_dir_rel)) return false;

    const char *obj = temp_sprintf("%s/%s.o", build_dir_rel, name);
    Cmd cmd = {0};
    cmd_append(&cmd, t->cc ? t->cc : "clang");
    cmd_append(&cmd, "-c", "-O2");
#if !PLATFORM_WIN32
    cmd_append(&cmd, "-fPIC");
#endif
    for (size_t i = 0; i < extra_cflags_count; i++) cmd_append(&cmd, extra_cflags[i]);
    cmd_append(&cmd, abs_src, "-o", obj);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    const char *lib_dir = temp_sprintf("%s/lib", abs_prefix);
    const char *inc_dir = temp_sprintf("%s/include", abs_prefix);
    if (!mkdir_if_not_exists(lib_dir)) return false;
    if (!mkdir_if_not_exists(inc_dir)) return false;

    const char *lib = temp_sprintf("%s/lib%s.a", lib_dir, name);
    if (file_exists(lib)) delete_file(lib);

    Cmd ar_cmd = {0};
    cmd_append(&ar_cmd, t->ar ? t->ar : "ar", "rcs", lib, obj);
    if (!cmd_run_sync_and_reset(&ar_cmd)) return false;

    if (t->ranlib) {
        Cmd ranlib_cmd = {0};
        cmd_append(&ranlib_cmd, t->ranlib, lib);
        if (!cmd_run_sync_and_reset(&ranlib_cmd)) return false;
    }

    for (size_t i = 0; i < headers_count; i++) {
        const char *hname = strrchr(headers[i], '/');
        hname = hname ? hname + 1 : headers[i];
        if (!copy_file(headers[i], temp_sprintf("%s/%s", inc_dir, hname))) return false;
    }
    return true;
}

static bool cmake_build(const Target *t, const char *src_rel, const char *name,
                        const char *const *extra_args, size_t extra_args_count) {
    const char *cwd = get_current_dir_temp();
    const char *abs_src = temp_sprintf("%s/%s", cwd, src_rel);
    const char *abs_prefix = target_prefix_abs(t);
    const char *build_dir_rel = temp_sprintf("%s/%s-build", target_prefix_rel(t), name);

    if (!mkdir_if_not_exists(build_dir_rel)) return false;

    Cmd cmd = {0};
    cmd_append(&cmd, "cmake", "-S", abs_src, "-B", build_dir_rel);
    cmd_append(&cmd, temp_sprintf("-DCMAKE_INSTALL_PREFIX=%s", abs_prefix));
    cmd_append(&cmd, "-DCMAKE_BUILD_TYPE=Release");
    cmd_append(&cmd, "-DBUILD_SHARED_LIBS=OFF");

    if (t->android_ndk) {
        cmd_append(&cmd, temp_sprintf("-DCMAKE_TOOLCHAIN_FILE=%s/build/cmake/android.toolchain.cmake", t->android_ndk));
        cmd_append(&cmd, temp_sprintf("-DANDROID_ABI=%s", t->android_abi));
        cmd_append(&cmd, temp_sprintf("-DANDROID_PLATFORM=android-%d", t->android_api));
    } else if (t->cc) {
        cmd_append(&cmd, temp_sprintf("-DCMAKE_C_COMPILER=%s", t->cc));
    }

    for (size_t i = 0; i < extra_args_count; i++) cmd_append(&cmd, extra_args[i]);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    Cmd build = {0};
    cmd_append(&build, "cmake", "--build", build_dir_rel, "--config", "Release",
               "--parallel", temp_sprintf("%d", nob_nprocs()));
    if (!cmd_run_sync_and_reset(&build)) return false;

    Cmd install = {0};
    cmd_append(&install, "cmake", "--install", build_dir_rel, "--config", "Release");
    return cmd_run_sync_and_reset(&install);
}

static bool bootstrap_third_party(const Target *t) {
    const char *prefix = target_prefix_rel(t);
    const char *gmp_a    = autotools_static_lib(t, "gmp");
    const char *mpfr_a   = autotools_static_lib(t, "mpfr");
#if PLATFORM_WIN32
    const char *sdl_a    = temp_sprintf("%s/lib/SDL3-static.lib", prefix);
#else
    const char *sdl_a    = temp_sprintf("%s/lib/libSDL3.a",    prefix);
#endif
    const char *sqlite_a = temp_sprintf("%s/lib/libsqlite3.a", prefix);

    if (file_exists(gmp_a) && file_exists(mpfr_a) && file_exists(sdl_a) && file_exists(sqlite_a)) return true;

    nob_log(NOB_INFO, "bootstrapping third-party (gmp, mpfr, sdl3, sqlite3) for target '%s'", t->name);

    if (!mkdir_if_not_exists(BUILD_DIR))      return false;
    if (!mkdir_if_not_exists(TP_BUILD_DIR))   return false;
    if (!mkdir_if_not_exists(prefix))         return false;

    if (!file_exists(gmp_a)) {
        if (!autotools_build(t, "third-party/gmp", "gmp", NULL)) return false;
    }
    if (!file_exists(mpfr_a)) {
        const char *with_gmp = temp_sprintf("--with-gmp=%s", to_autotools_path(target_prefix_abs(t)));
        if (!autotools_build(t, "third-party/mpfr", "mpfr", with_gmp)) return false;
    }
    if (!file_exists(sdl_a)) {
        static const char *sdl_args[] = {
            "-DSDL_SHARED=OFF",
            "-DSDL_STATIC=ON",
            "-DSDL_TEST_LIBRARY=OFF",
            "-DSDL_TESTS=OFF",
            "-DSDL_EXAMPLES=OFF",
            "-DSDL_INSTALL_TESTS=OFF",
            "-DSDL_DISABLE_INSTALL_DOCS=ON",
            "-DSDL_AUDIO=OFF",
            "-DSDL_VIDEO=OFF",
            "-DSDL_GPU=OFF",
            "-DSDL_RENDER=OFF",
            "-DSDL_CAMERA=OFF",
            "-DSDL_JOYSTICK=OFF",
            "-DSDL_HAPTIC=OFF",
            "-DSDL_HIDAPI=OFF",
            "-DSDL_POWER=OFF",
            "-DSDL_SENSOR=OFF",
            "-DSDL_DIALOG=OFF",
            "-DSDL_OPENGL=OFF",
            "-DSDL_OPENGLES=OFF",
            "-DSDL_VULKAN=OFF",
        };
        if (!cmake_build(t, "third-party/sdl3", "sdl3", sdl_args, NOB_ARRAY_LEN(sdl_args))) return false;
    }
    if (!file_exists(sqlite_a)) {
        static const char *sqlite_headers[] = {
            "third-party/sqlite3/sqlite3.h",
            "third-party/sqlite3/sqlite3ext.h",
        };
        static const char *sqlite_cflags[] = {
            "-DSQLITE_OMIT_LOAD_EXTENSION",
            "-DSQLITE_THREADSAFE=1",
        };
        if (!single_tu_build(t, "third-party/sqlite3/sqlite3.c", "sqlite3",
                             sqlite_headers, NOB_ARRAY_LEN(sqlite_headers),
                             sqlite_cflags,  NOB_ARRAY_LEN(sqlite_cflags))) return false;
    }
    return true;
}


