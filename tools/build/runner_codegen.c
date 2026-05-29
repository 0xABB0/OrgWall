#include "runner_internal.h"

static const char *enum_str_tool_path(void) {
    return MEL_BUILD_DIR "/build-modules/enum_str_gen";
}

static const char *llvm_prefix(void) {
    const char *e = getenv("MEL_LIBCLANG_PREFIX");
    if (e && *e) return e;
    return "/opt/homebrew/opt/llvm";
}

static const char *llvm_builtin_include(void) {
    const char *base = temp_sprintf("%s/lib/clang", llvm_prefix());
    File_Paths entries = {0};
    if (!read_entire_dir(base, &entries)) return NULL;
    const char *found = NULL;
    for (size_t i = 0; i < entries.count; i++) {
        const char *n = entries.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char *cand = temp_sprintf("%s/%s/include", base, n);
        if (get_file_type(cand) == NOB_FILE_DIRECTORY) { found = cand; break; }
    }
    free(entries.items);
    return found;
}

#if defined(__APPLE__)
static const char *host_sdk_path(void) {
    static const char *cached = NULL;
    if (cached) return cached;
    FILE *p = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (!p) return NULL;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    pclose(p);
    if (n == 0) return NULL;
    buf[n] = 0;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
    cached = temp_strdup(buf);
    return cached;
}
#endif

static bool ensure_enum_str_tool(void) {
    static bool built = false;
    if (built) return true;

    const char *prefix = llvm_prefix();
    const char *inc = temp_sprintf("%s/include", prefix);
    const char *lib = temp_sprintf("%s/lib", prefix);
    if (!file_exists(temp_sprintf("%s/clang-c/Index.h", inc))) {
        nob_log(NOB_ERROR, "libclang not found under %s; install LLVM (brew install llvm) or set MEL_LIBCLANG_PREFIX", prefix);
        return false;
    }

    if (!mkdir_if_not_exists(MEL_BUILD_DIR)) return false;
    if (!mkdir_if_not_exists(MEL_BUILD_DIR "/build-modules")) return false;

    const char *src = "tools/codegen/enum_str_gen.c";
    const char *out = enum_str_tool_path();
    if (!file_exists(out) || needs_rebuild1(out, src) != 0) {
        Cmd cc = {0};
        cmd_append(&cc, "clang", "-std=c23", "-O2", temp_sprintf("-I%s", inc));
        cmd_append(&cc, src);
        cmd_append(&cc, temp_sprintf("-L%s", lib), "-lclang", temp_sprintf("-Wl,-rpath,%s", lib));
        cmd_append(&cc, "-o", out);
        if (!cmd_run_sync_and_reset(&cc)) {
            nob_log(NOB_ERROR, "failed to build enum_str_gen");
            return false;
        }
    }
    built = true;
    return true;
}

static bool run_enum_str_codegen(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    if (!t->enum_str_headers.count) return true;
    if (!ensure_enum_str_tool()) return false;

    const char *gendir = temp_sprintf("%s/%s/generated", ctx->out_dir, t->name);
    if (!mel_mkdirs(gendir)) return false;
    const char *outc = temp_sprintf("%s/enum_to_string.generated.c", gendir);

    File_Paths inputs = {0};
    for (size_t i = 0; i < t->enum_str_headers.count; i++) {
        const char *sp = t->enum_str_headers.items[i];
        const char *found = NULL;
        for (size_t j = 0; j < ctx->resolved.includes.count; j++) {
            const char *cand = temp_sprintf("%s/%s", ctx->resolved.includes.items[j], sp);
            if (file_exists(cand)) { found = cand; break; }
        }
        if (!found) {
            nob_log(NOB_ERROR, "enum_str: header '%s' not found on the include path", sp);
            return false;
        }
        da_append(&inputs, found);
    }

    bool need = !file_exists(outc) || needs_rebuild1(outc, enum_str_tool_path()) != 0;
    for (size_t i = 0; i < inputs.count && !need; i++)
        if (needs_rebuild1(outc, inputs.items[i]) != 0) need = true;

    if (need) {
        Cmd c = {0};
        cmd_append(&c, enum_str_tool_path(), outc, "-std=c23");
#if defined(__APPLE__)
        const char *sdk = host_sdk_path();
        if (sdk) cmd_append(&c, "-isysroot", sdk);
#endif
        const char *builtin = llvm_builtin_include();
        if (builtin) cmd_append(&c, "-isystem", builtin);
        for (size_t i = 0; i < ctx->resolved.includes.count; i++) {
            const char *inc = ctx->resolved.includes.items[i];
            if (strstr(inc, "third-party") != NULL) cmd_append(&c, "-isystem", inc);
            else                                    cmd_append(&c, temp_sprintf("-I%s", inc));
        }
        for (size_t i = 0; i < ctx->resolved.defines.count; i++)
            cmd_append(&c, temp_sprintf("-D%s", ctx->resolved.defines.items[i]));
        cmd_append(&c, "--");
        for (size_t i = 0; i < t->enum_str_headers.count; i++) cmd_append(&c, t->enum_str_headers.items[i]);
        if (!cmd_run_sync_and_reset(&c)) {
            nob_log(NOB_ERROR, "enum_str codegen failed for target '%s'", t->name);
            return false;
        }
        nob_log(NOB_INFO, "generated %s", outc);
    }

    da_append(&ctx->sources, temp_strdup(outc));
    return true;
}
