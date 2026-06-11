#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define XSTR(x) #x
#define STR(x)  XSTR(x)

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
            dup2(dn, 1);
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

static int copy_file(const char* src, const char* dst)
{
    FILE* in = fopen(src, "rb");
    if (!in)
        return -1;
    fseek(in, 0, SEEK_END);
    long n = ftell(in);
    fseek(in, 0, SEEK_SET);
    char* data = malloc((size_t)n);
    int   ok = (int)fread(data, 1, (size_t)n, in) == n;
    fclose(in);
    if (!ok)
    {
        free(data);
        return -1;
    }
    FILE* out = fopen(dst, "wb");
    if (!out)
    {
        free(data);
        return -1;
    }
    ok = (int)fwrite(data, 1, (size_t)n, out) == n;
    fclose(out);
    free(data);
    return ok ? 0 : -1;
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

    char  tmpdir[] = "/tmp/coro_reject_XXXXXX";
    char* scratch = mkdtemp(tmpdir);
    if (!scratch)
    {
        fprintf(stderr, "reject[%s]: mkdtemp failed\n", name);
        return 1;
    }

    size_t nlen = strlen(name);
    char*  fname = malloc(nlen + 8);
    memcpy(fname, name, nlen);
    strcpy(fname + nlen, ".coro.c");

    char* reject_dir = path_join(mdir, "test/reject");
    char* outc = path_join(scratch, "out.gen.c");
    char* src = path_join(reject_dir, fname);
    char* tmp = path_join(scratch, fname);
    char* icoro = path_join(mdir, "include");

    char* sdk = popen_trim("xcrun --show-sdk-path 2>/dev/null");
    char* builtin = clang_builtin_include();

    if (copy_file(src, tmp) != 0)
    {
        fprintf(stderr, "reject[%s]: failed to copy fixture\n", name);
        return 1;
    }

    Argv argv_gen = { 0 };
    argv_push(&argv_gen, (char*)gen);
    argv_push(&argv_gen, tmp);
    argv_push(&argv_gen, outc);
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

    int rc = run_quiet(argv_gen.items);
    if (rc == 0)
    {
        fprintf(stderr, "reject[%s]: expected coro-gen to fail but it succeeded\n", name);
        return 1;
    }
    fprintf(stderr, "ok reject[%s]\n", name);
    return 0;
}
