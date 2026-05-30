#define NOB_IMPLEMENTATION
#include "../../nob.h"

#include <string.h>

#define OUT     ".cache"
#define GEN     OUT "/gen"
#define BIN     OUT "/bin"
#define TOOL    OUT "/continuation_gen"
#define TOOLSRC "codegen/continuation_gen.c"

static const char* g_fixtures[] = { "sum_to", "countdown", "classify", "relay", "repeat_sum" };
static const char* g_drivers[]  = { "sum_to", "countdown", "classify", "relay", "repeat_sum" };
static const char* g_extra[]    = { "snapshot" };
static const char* g_rejects[]  = { "switch_cross", "goto_cross", "addr_of_lifted", "vla_lifted" };

static const char* llvm_prefix(void)
{
    const char* e = getenv("MEL_LIBCLANG_PREFIX");
    return (e && *e) ? e : "/opt/homebrew/opt/llvm";
}

static const char* llvm_builtin_include(void)
{
    const char*    base    = nob_temp_sprintf("%s/lib/clang", llvm_prefix());
    Nob_File_Paths entries = {0};
    if (!nob_read_entire_dir(base, &entries)) return NULL;
    const char* found = NULL;
    for (size_t i = 0; i < entries.count; i++)
    {
        const char* n = entries.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char* cand = nob_temp_sprintf("%s/%s/include", base, n);
        if (nob_get_file_type(cand) == NOB_FILE_DIRECTORY) { found = cand; break; }
    }
    return found;
}

static const char* host_sdk_path(void)
{
#if defined(__APPLE__)
    static char buf[1024];
    static int  done;
    if (done) return buf[0] ? buf : NULL;
    done    = 1;
    FILE* p = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (!p) return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    pclose(p);
    buf[n] = 0;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
    return buf[0] ? buf : NULL;
#else
    return NULL;
#endif
}

static void codegen_clang_args(Nob_Cmd* c)
{
    nob_cmd_append(c, "-DMEL_CONT_CODEGEN", "-std=c23");
    const char* sdk = host_sdk_path();
    if (sdk) nob_cmd_append(c, "-isysroot", sdk);
    const char* builtin = llvm_builtin_include();
    if (builtin) nob_cmd_append(c, "-isystem", builtin);
    nob_cmd_append(c, "-Iinclude", "-I../core/include");
}

static bool ensure_dirs(void)
{
    return nob_mkdir_if_not_exists(OUT) && nob_mkdir_if_not_exists(GEN) && nob_mkdir_if_not_exists(BIN);
}

static bool build_tool(void)
{
    const char* prefix = llvm_prefix();
    const char* inc    = nob_temp_sprintf("%s/include", prefix);
    const char* lib    = nob_temp_sprintf("%s/lib", prefix);
    if (!nob_file_exists(nob_temp_sprintf("%s/clang-c/Index.h", inc)))
    {
        nob_log(NOB_ERROR, "libclang not found under %s; `brew install llvm` or set MEL_LIBCLANG_PREFIX", prefix);
        return false;
    }
    if (nob_file_exists(TOOL) && nob_needs_rebuild1(TOOL, TOOLSRC) == 0) return true;

    Nob_Cmd c = {0};
    nob_cmd_append(&c, "clang", "-std=c23", "-O2", "-g", "-Wall", "-Wextra");
    nob_cmd_append(&c, nob_temp_sprintf("-I%s", inc), TOOLSRC);
    nob_cmd_append(&c, nob_temp_sprintf("-L%s", lib), "-lclang", nob_temp_sprintf("-Wl,-rpath,%s", lib));
    nob_cmd_append(&c, "-o", TOOL);
    return nob_cmd_run_sync_and_reset(&c);
}

static bool run_codegen(const char* dir, const char* name, const char* out_h, const char* out_c)
{
    Nob_Cmd c = {0};
    nob_cmd_append(&c, TOOL, out_h, out_c, nob_temp_sprintf("%s/%s.cont.c", dir, name));
    codegen_clang_args(&c);
    return nob_cmd_run_sync_and_reset(&c);
}

static bool files_equal(const char* a, const char* b)
{
    Nob_String_Builder sa = {0};
    Nob_String_Builder sb = {0};
    bool               ok = nob_read_entire_file(a, &sa) && nob_read_entire_file(b, &sb);
    bool eq = ok && sa.count == sb.count && memcmp(sa.items, sb.items, sa.count) == 0;
    nob_sb_free(sa);
    nob_sb_free(sb);
    return eq;
}

static bool golden_check(const char* name, bool bless)
{
    const char* gen_h = nob_temp_sprintf("%s/%s.gen.h", GEN, name);
    const char* gen_c = nob_temp_sprintf("%s/%s.gen.c", GEN, name);
    const char* gld_h = nob_temp_sprintf("test/golden/%s.gen.h", name);
    const char* gld_c = nob_temp_sprintf("test/golden/%s.gen.c", name);
    if (bless)
    {
        return nob_copy_file(gen_h, gld_h) && nob_copy_file(gen_c, gld_c);
    }
    if (!nob_file_exists(gld_h) || !nob_file_exists(gld_c))
    {
        nob_log(NOB_ERROR, "golden missing for %s (run `./build bless`)", name);
        return false;
    }
    if (!files_equal(gen_h, gld_h) || !files_equal(gen_c, gld_c))
    {
        nob_log(NOB_ERROR, "golden mismatch for %s (diff %s %s ; bless with `./build bless`)", name, GEN, "test/golden");
        return false;
    }
    return true;
}

static bool build_and_run_driver(const char* name)
{
    const char* gen_c = nob_temp_sprintf("%s/%s.gen.c", GEN, name);
    const char* drv   = nob_temp_sprintf("test/driver/%s_diff.c", name);
    const char* bin   = nob_temp_sprintf("%s/%s_diff", BIN, name);
    Nob_Cmd     c     = {0};
    nob_cmd_append(&c, "clang", "-std=c23", "-O0", "-g", "-Wall", "-Wextra");
    nob_cmd_append(&c, nob_temp_sprintf("-I%s", GEN), "-Itest/driver", "-Iinclude", "-I../core/include");
    nob_cmd_append(&c, drv, gen_c, "-o", bin);
    if (!nob_cmd_run_sync_and_reset(&c)) return false;
    nob_cmd_append(&c, bin);
    return nob_cmd_run_sync_and_reset(&c);
}

static bool build_and_run_extra(const char* name)
{
    const char* gen_c = nob_temp_sprintf("%s/sum_to.gen.c", GEN);
    const char* drv   = nob_temp_sprintf("test/driver/%s.c", name);
    const char* bin   = nob_temp_sprintf("%s/%s", BIN, name);
    Nob_Cmd     c     = {0};
    nob_cmd_append(&c, "clang", "-std=c23", "-O0", "-g", "-Wall", "-Wextra");
    nob_cmd_append(&c, nob_temp_sprintf("-I%s", GEN), "-Itest/driver", "-Iinclude", "-I../core/include");
    nob_cmd_append(&c, drv, gen_c, "-o", bin);
    if (!nob_cmd_run_sync_and_reset(&c)) return false;
    nob_cmd_append(&c, bin);
    return nob_cmd_run_sync_and_reset(&c);
}

static bool run_reject(const char* name)
{
    nob_log(NOB_INFO, "reject fixture '%s' (the error below is expected):", name);
    Nob_Cmd c = {0};
    nob_cmd_append(&c, TOOL, GEN "/_reject.h", GEN "/_reject.c", nob_temp_sprintf("test/reject/%s.cont.c", name));
    codegen_clang_args(&c);
    bool succeeded = nob_cmd_run_sync_and_reset(&c);
    if (succeeded) nob_log(NOB_ERROR, "reject fixture %s was accepted (expected a hard error)", name);
    return !succeeded;
}

static bool cmd_gen(bool bless)
{
    bool ok = true;
    for (size_t i = 0; i < NOB_ARRAY_LEN(g_fixtures); i++)
    {
        const char* fx    = g_fixtures[i];
        const char* out_h = nob_temp_sprintf("%s/%s.gen.h", GEN, fx);
        const char* out_c = nob_temp_sprintf("%s/%s.gen.c", GEN, fx);
        if (!run_codegen("test/fixtures", fx, out_h, out_c)) { ok = false; continue; }
        if (!golden_check(fx, bless)) ok = false;
    }
    return ok;
}

static bool cmd_test(void)
{
    bool ok = cmd_gen(false);
    for (size_t i = 0; i < NOB_ARRAY_LEN(g_drivers); i++)
        if (!build_and_run_driver(g_drivers[i])) ok = false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(g_extra); i++)
        if (!build_and_run_extra(g_extra[i])) ok = false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(g_rejects); i++)
        if (!run_reject(g_rejects[i])) ok = false;
    nob_log(ok ? NOB_INFO : NOB_ERROR, ok ? "all continuation tests passed" : "continuation tests FAILED");
    return ok;
}

int main(int argc, char** argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!ensure_dirs()) return 1;
    if (!build_tool()) return 1;

    const char* cmd = argc >= 2 ? argv[1] : "test";

    if (strcmp(cmd, "tool") == 0) return 0;
    if (strcmp(cmd, "gen") == 0) return cmd_gen(false) ? 0 : 1;
    if (strcmp(cmd, "bless") == 0) return cmd_gen(true) ? 0 : 1;
    if (strcmp(cmd, "test") == 0) return cmd_test() ? 0 : 1;

    nob_log(NOB_ERROR, "usage: %s [tool|gen|bless|test]", argv[0]);
    return 2;
}
