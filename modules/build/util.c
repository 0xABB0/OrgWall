#include "runner.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

char *mel_str_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char  *p = malloc(n);
    if (!p) abort();
    memcpy(p, s, n);
    return p;
}

char *mel_str_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = malloc((size_t)n + 1);
    if (!p) abort();
    vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

bool mel_path_is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

bool mel_path_is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

bool mel_path_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

char *mel_path_join(const char *a, const char *b) {
    if (!a || !*a) return mel_str_dup(b);
    if (!b || !*b) return mel_str_dup(a);
    bool slash = a[strlen(a) - 1] == '/';
    return mel_str_fmt("%s%s%s", a, slash ? "" : "/", b);
}

void mel_mkdirs(const char *path) {
    char *p = mel_str_dup(path);
    for (char *s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    }
    mkdir(p, 0755);
    free(p);
}

int mel_run(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

char *mel_read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) abort();
    size_t rd = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    buf[rd] = 0;
    return buf;
}

bool mel_write_file(const char *path, const char *data) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    fputs(data, fp);
    fclose(fp);
    return true;
}

bool mel_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char  *buf = malloc(1 << 16);
    size_t n;
    while ((n = fread(buf, 1, 1 << 16, in)) > 0) fwrite(buf, 1, n, out);
    free(buf);
    fclose(in);
    fclose(out);
    chmod(dst, 0755);
    return true;
}

int mel_run_quiet(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) {
            dup2(dn, 1);
            dup2(dn, 2);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int mel_run_vec(Mel_StrVec *cmd) {
    for (size_t i = 0; i < cmd->len; i++) fprintf(stderr, "%s%s", i ? " " : "", cmd->items[i]);
    fputc('\n', stderr);
    mel_da_push(cmd, NULL);
    int rc = mel_run((char *const *)cmd->items);
    cmd->len--;
    return rc;
}
