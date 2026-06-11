#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define XSTR(x) #x
#define STR(x)  XSTR(x)

static int run(char* const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0)
    {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0)
        return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int run_quiet(char* const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0)
    {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0)
        {
            dup2(dn, 2);
            close(dn);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0)
        return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static char* read_file(const char* path, size_t* out_len)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* p = malloc((size_t)n + 1);
    if (fread(p, 1, (size_t)n, f) != (size_t)n)
    {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    p[n] = 0;
    *out_len = (size_t)n;
    return p;
}

static int copy_file(const char* src, const char* dst)
{
    size_t n;
    char*  data = read_file(src, &n);
    if (!data)
        return -1;
    FILE* f = fopen(dst, "wb");
    if (!f)
    {
        free(data);
        return -1;
    }
    int ok = fwrite(data, 1, n, f) == n;
    fclose(f);
    free(data);
    return ok ? 0 : -1;
}

static int diff_files(const char* path_a, const char* path_b)
{
    size_t la, lb;
    char*  a = read_file(path_a, &la);
    char*  b = read_file(path_b, &lb);
    if (!a || !b)
    {
        free(a);
        free(b);
        return -1;
    }
    int eq = la == lb && memcmp(a, b, la) == 0;
    free(a);
    free(b);
    return eq ? 0 : 1;
}

static char* path_join(const char* a, const char* b)
{
    size_t la = strlen(a), lb = strlen(b);
    char*  s = malloc(la + lb + 2);
    memcpy(s, a, la);
    s[la] = '/';
    memcpy(s + la + 1, b, lb + 1);
    return s;
}

static char* popen_trim(const char* cmd)
{
    FILE* p = popen(cmd, "r");
    if (!p)
        return NULL;
    char   buf[1024] = { 0 };
    size_t k = fread(buf, 1, sizeof buf - 1, p);
    pclose(p);
    while (k && (buf[k - 1] == '\n' || buf[k - 1] == ' ' || buf[k - 1] == '\r'))
        k--;
    buf[k] = 0;
    return k ? strdup(buf) : NULL;
}

static char* clang_builtin_include(void)
{
    static const char* base = "/opt/homebrew/opt/llvm/lib/clang";
    DIR*               d = opendir(base);
    if (!d)
        return NULL;
    char* ver = NULL;
    for (struct dirent* e; (e = readdir(d));)
    {
        if (e->d_name[0] == '.')
            continue;
        ver = strdup(e->d_name);
        break;
    }
    closedir(d);
    if (!ver)
        return NULL;
    char* inc = path_join(path_join(base, ver), "include");
    free(ver);
    return inc;
}

typedef struct
{
    char** items;
    int    len;
    int    cap;
} Argv;

static void argv_push(Argv* a, char* s)
{
    if (s == NULL)
        return;
    if (a->len == a->cap)
    {
        a->cap = a->cap ? a->cap * 2 : 16;
        a->items = realloc(a->items, (size_t)a->cap * sizeof *a->items);
    }
    a->items[a->len++] = s;
}

int main(void)
{
    const char* name = STR(FIXTURE_NAME);
    const char* gen = STR(CORO_GEN);
    const char* mdir = STR(CORO_MODULE_DIR);
    const char* icore = STR(CORE_INCLUDE_DIR);

    char  tmpdir[] = "/tmp/coro_golden_XXXXXX";
    char* scratch = mkdtemp(tmpdir);
    if (!scratch)
    {
        fprintf(stderr, "golden[%s]: mkdtemp failed\n", name);
        return 1;
    }

    size_t nlen = strlen(name);
    char*  hname = malloc(nlen + 8);
    char*  cname = malloc(nlen + 8);
    memcpy(hname, name, nlen);
    strcpy(hname + nlen, ".coro.h");
    memcpy(cname, name, nlen);
    strcpy(cname + nlen, ".gen.c");

    char* fixtures_dir = path_join(mdir, "test/fixtures");
    char* golden_dir = path_join(mdir, "test/golden");
    char* src_h = path_join(fixtures_dir, hname);
    char* tmp_h = path_join(scratch, hname);
    char* tmp_c = path_join(scratch, cname);
    char* gold_h = path_join(golden_dir, hname);
    char* gold_c = path_join(golden_dir, cname);
    char* icoro = path_join(mdir, "include");

    char* sdk = popen_trim("xcrun --show-sdk-path 2>/dev/null");
    char* builtin = clang_builtin_include();

    if (copy_file(src_h, tmp_h) != 0)
    {
        fprintf(stderr, "golden[%s]: failed to copy fixture\n", name);
        return 1;
    }

    Argv argv_gen = { 0 };
    argv_push(&argv_gen, (char*)gen);
    argv_push(&argv_gen, tmp_h);
    argv_push(&argv_gen, tmp_c);
    argv_push(&argv_gen, "-DMEL_CORO_CODEGEN");
    argv_push(&argv_gen, "-I");
    argv_push(&argv_gen, icoro);
    argv_push(&argv_gen, "-I");
    argv_push(&argv_gen, (char*)icore);
    if (sdk)
    {
        argv_push(&argv_gen, "-isysroot");
        argv_push(&argv_gen, sdk);
    }
    if (builtin)
    {
        argv_push(&argv_gen, "-isystem");
        argv_push(&argv_gen, builtin);
    }
    argv_push(&argv_gen, NULL);

    if (run_quiet(argv_gen.items) != 0)
    {
        fprintf(stderr, "golden[%s]: coro-gen failed\n", name);
        return 1;
    }

    int fail = 0;

    if (diff_files(tmp_c, gold_c) != 0)
    {
        fprintf(stderr, "golden[%s]: .gen.c mismatch\n", name);
        char* dargv[] = { "diff", gold_c, tmp_c, NULL };
        run(dargv);
        fail++;
    }
    if (diff_files(tmp_h, gold_h) != 0)
    {
        fprintf(stderr, "golden[%s]: .coro.h mismatch\n", name);
        char* dargv[] = { "diff", gold_h, tmp_h, NULL };
        run(dargv);
        fail++;
    }

    if (!fail)
        fprintf(stderr, "ok golden[%s]\n", name);
    return fail ? 1 : 0;
}
