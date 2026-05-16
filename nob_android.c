// Android build support — NDK discovery, .so building, APK assembly, ADB commands.
// Included by nob.c — relies on nob.h, Target, Layout, Android_Abi, android_abis,
// and nob_third_party.c being included before this file.

static Target *target_android(const Android_Abi *abi, const char *toolchain_bin, const char *ndk) {
    Target *t = (Target*)temp_alloc(sizeof(Target));
    memset(t, 0, sizeof(*t));
    t->name = temp_sprintf("android-%s", abi->abi);
    t->configure_host = abi->configure_host;
    t->cc = temp_sprintf("%s/%s", toolchain_bin, abi->clang);
    t->ar = temp_sprintf("%s/llvm-ar", toolchain_bin);
    t->ranlib = temp_sprintf("%s/llvm-ranlib", toolchain_bin);
    t->android_ndk = ndk;
    t->android_abi = abi->abi;
    t->android_api = 23;
    return t;
}

static const char *android_sdk_dir(const char *app_name) {
    const char *sdk = getenv("ANDROID_HOME");
    if (sdk && sdk[0]) return sdk;

    sdk = getenv("ANDROID_SDK_ROOT");
    if (sdk && sdk[0]) return sdk;

    const char *local = temp_sprintf("%s/%s/android/local.properties", APPS_DIR, app_name);
    FILE *f = fopen(local, "r");
    if (!f) return NULL;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        const char *prefix = "sdk.dir=";
        size_t prefix_len = strlen(prefix);
        if (strncmp(line, prefix, prefix_len) != 0) continue;

        char *value = line + prefix_len;
        size_t len = strlen(value);
        while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
            value[--len] = 0;
        }
        fclose(f);
        return temp_strdup(value);
    }

    fclose(f);
    return NULL;
}

static const char *android_ndk_dir(const char *sdk) {
    const char *ndk_root = temp_sprintf("%s/ndk", sdk);
    File_Paths entries = {0};
    if (!read_entire_dir(ndk_root, &entries)) return NULL;

    const char *best = NULL;
    for (size_t i = 0; i < entries.count; i++) {
        const char *name = entries.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        const char *path = temp_sprintf("%s/%s", ndk_root, name);
        if (get_file_type(path) != NOB_FILE_DIRECTORY) continue;
        if (best == NULL || strcmp(name, best) > 0) best = temp_strdup(name);
    }

    if (best == NULL) return NULL;
    return temp_sprintf("%s/%s", ndk_root, best);
}

static const char *android_toolchain_bin(const char *ndk) {
    static const char *hosts[] = {
        "darwin-x86_64",
        "darwin-arm64",
        "linux-x86_64",
        "windows-x86_64",
    };

    for (size_t i = 0; i < NOB_ARRAY_LEN(hosts); i++) {
        const char *path = temp_sprintf("%s/toolchains/llvm/prebuilt/%s/bin", ndk, hosts[i]);
        if (get_file_type(path) == NOB_FILE_DIRECTORY) return path;
    }

    return NULL;
}

static const char *android_gradle_dir(const char *app_name) {
    return temp_sprintf("%s/%s/android", APPS_DIR, app_name);
}

static const char *android_jnilibs_dir(const char *app_name) {
    return temp_sprintf("%s/app/src/main/jniLibs", android_gradle_dir(app_name));
}

static const char *android_apk_path(const char *app_name) {
    return temp_sprintf("%s/app/build/outputs/apk/debug/app-debug.apk", android_gradle_dir(app_name));
}

static const char *android_application_id(const char *app_name) {
    const char *path = temp_sprintf("%s/app/build.gradle.kts", android_gradle_dir(app_name));
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) return NULL;

    const char *needle = "applicationId";
    size_t nlen = strlen(needle);
    const char *p = sb.items;
    const char *end = sb.items + sb.count;
    const char *result = NULL;

    while (p + nlen < end) {
        if (memcmp(p, needle, nlen) == 0) {
            const char *q = p + nlen;
            while (q < end && (*q == ' ' || *q == '\t' || *q == '=')) q++;
            if (q < end && *q == '"') {
                q++;
                const char *s = q;
                while (q < end && *q != '"') q++;
                if (q < end) {
                    size_t len = (size_t)(q - s);
                    char *out = (char*)temp_alloc(len + 1);
                    memcpy(out, s, len);
                    out[len] = 0;
                    result = out;
                    break;
                }
            }
        }
        p++;
    }

    free(sb.items);
    return result;
}

static const char *adb_path(const char *app_name) {
    const char *sdk = android_sdk_dir(app_name);
    if (sdk == NULL) return "adb";
    return temp_sprintf("%s/platform-tools/adb", sdk);
}

static bool android_build_so(const char *app_name) {
    const char *sdk = android_sdk_dir(app_name);
    if (sdk == NULL) {
        nob_log(NOB_ERROR, "Android SDK not found. Set ANDROID_HOME/ANDROID_SDK_ROOT or %s/%s/android/local.properties", APPS_DIR, app_name);
        return false;
    }
    const char *ndk = android_ndk_dir(sdk);
    if (ndk == NULL) {
        nob_log(NOB_ERROR, "Android NDK not found under %s/ndk", sdk);
        return false;
    }
    const char *toolchain_bin = android_toolchain_bin(ndk);
    if (toolchain_bin == NULL) {
        nob_log(NOB_ERROR, "Android NDK LLVM toolchain not found under %s/toolchains/llvm/prebuilt", ndk);
        return false;
    }

    nob_log(NOB_INFO, "using Android SDK: %s", sdk);
    nob_log(NOB_INFO, "using Android NDK: %s", ndk);

    Layout L = {0};
    if (!discover_for_platform(&L, "android")) return false;

    File_Paths lib_sources = {0};
    File_Paths bridge_sources = {0};
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *s = L.sources.items[i];
        if (ends_with(s, ".bridge.c")) da_append(&bridge_sources, s);
        else                            da_append(&lib_sources, s);
    }

    File_Paths app_sources = {0};
    if (!collect_dir_sources(temp_sprintf("%s/%s/src", APPS_DIR, app_name), &app_sources)) return false;
    if (app_sources.count == 0) {
        nob_log(NOB_ERROR, "no app sources found under %s/%s/src", APPS_DIR, app_name);
        return false;
    }

    const char *jnilibs = android_jnilibs_dir(app_name);
    if (!mkdir_if_not_exists(jnilibs)) return false;

    for (size_t i = 0; i < NOB_ARRAY_LEN(android_abis); i++) {
        const Android_Abi *abi = &android_abis[i];
        Target *t = target_android(abi, toolchain_bin, ndk);

        if (!bootstrap_third_party(t)) return false;

        if (!mkdir_if_not_exists(temp_sprintf("%s/%s", BUILD_DIR, t->name))) return false;
        if (!mkdir_if_not_exists(target_obj_dir(t))) return false;

        File_Paths lib_objects = {0};
        bool all_ok = true;
        for (size_t j = 0; j < lib_sources.count; j++) {
            const char *src = lib_sources.items[j];
            const char *obj = target_object_for(t, src);
            if (!target_compile_one(t, &L, src, obj, "-DANDROID")) { all_ok = false; continue; }
            da_append(&lib_objects, obj);
        }
        if (!all_ok) nob_log(NOB_WARNING, "one or more translation units failed to compile for %s", t->name);
        if (lib_objects.count == 0) {
            nob_log(NOB_ERROR, "no objects produced for %s; aborting", t->name);
            return false;
        }
        if (!target_archive(t, &lib_objects)) return false;
        nob_log(NOB_INFO, "wrote %s (%zu/%zu objects)", target_lib_path(t), lib_objects.count, lib_sources.count);

        File_Paths link_objects = {0};
        for (size_t j = 0; j < bridge_sources.count; j++) {
            const char *src = bridge_sources.items[j];
            const char *obj = target_object_for(t, src);
            if (!target_compile_one(t, &L, src, obj, "-DANDROID")) return false;
            da_append(&link_objects, obj);
        }
        for (size_t j = 0; j < app_sources.count; j++) {
            const char *src = app_sources.items[j];
            const char *obj = target_object_for(t, src);
            if (!target_compile_one(t, &L, src, obj, "-DANDROID")) return false;
            da_append(&link_objects, obj);
        }

        const char *abi_dir = temp_sprintf("%s/%s", jnilibs, abi->abi);
        if (!mkdir_if_not_exists(abi_dir)) return false;

        Cmd cmd = {0};
        cmd_append(&cmd, t->cc);
        cmd_append(&cmd, "-shared", "-fPIC");
        cmd_append(&cmd, "-Wl,--gc-sections", "-Wl,--as-needed");
        for (size_t j = 0; j < link_objects.count; j++) cmd_append(&cmd, link_objects.items[j]);
        cmd_append(&cmd, temp_sprintf("-L%s/%s", BUILD_DIR, t->name));
        cmd_append(&cmd, temp_sprintf("-L%s", target_lib(t)));
        cmd_append(&cmd, "-lmelody", "-lmpfr", "-lgmp", "-lSDL3", "-lsqlite3", "-llog");
        cmd_append(&cmd, "-o", temp_sprintf("%s/libmelody.so", abi_dir));
        if (!cmd_run_sync_and_reset(&cmd)) return false;
    }

    return true;
}

static bool android_assemble_apk(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, "gradle", "-p", android_gradle_dir(app_name), ":app:assembleDebug");
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_install(const char *app_name) {
    const char *apk = android_apk_path(app_name);
    if (!file_exists(apk)) {
        nob_log(NOB_ERROR, "APK not found at %s", apk);
        return false;
    }
    Cmd cmd = {0};
    cmd_append(&cmd, adb_path(app_name), "install", "-r", apk);
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_launch(const char *app_name) {
    const char *pkg = android_application_id(app_name);
    if (pkg == NULL) {
        nob_log(NOB_ERROR, "could not read applicationId from %s/app/build.gradle.kts", android_gradle_dir(app_name));
        return false;
    }
    Cmd cmd = {0};
    cmd_append(&cmd, adb_path(app_name), "shell", "monkey", "-p", pkg,
               "-c", "android.intent.category.LAUNCHER", "1");
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_logcat(const char *app_name) {
    const char *adb = adb_path(app_name);
    Cmd clear = {0};
    cmd_append(&clear, adb, "logcat", "-c");
    cmd_run_sync_and_reset(&clear);

    Cmd cmd = {0};
    cmd_append(&cmd, adb, "logcat", "Melody:V", "AndroidRuntime:E", "*:S");
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_build_app(const char *app_name) {
    if (!android_build_so(app_name)) return false;
    if (!android_assemble_apk(app_name)) return false;
    return true;
}
