#ifndef MEL_BUILD_WIN32_COMPAT_H
#define MEL_BUILD_WIN32_COMPAT_H

#ifdef _WIN32

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>

#define popen  _popen
#define pclose _pclose
#define getcwd _getcwd
#define mkdir(path, mode) _mkdir(path)
#define chmod(path, mode) 0

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE           handle;
    WIN32_FIND_DATAA find;
    bool             pending;
    struct dirent    entry;
} DIR;

static inline DIR *opendir(const char *path) {
    char pattern[MAX_PATH];
    int  n = snprintf(pattern, sizeof(pattern), "%s/*", path);
    if (n < 0 || n >= (int)sizeof(pattern)) return NULL;

    DIR *d = (DIR *)calloc(1, sizeof(DIR));
    if (!d) return NULL;

    d->handle = FindFirstFileA(pattern, &d->find);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->pending = true;
    return d;
}

static inline struct dirent *readdir(DIR *d) {
    if (!d->pending) {
        if (!FindNextFileA(d->handle, &d->find)) return NULL;
    }
    d->pending = false;
    snprintf(d->entry.d_name, sizeof(d->entry.d_name), "%s", d->find.cFileName);
    return &d->entry;
}

static inline int closedir(DIR *d) {
    if (!d) return -1;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    free(d);
    return 0;
}

#define RTLD_NOW   0
#define RTLD_LOCAL 0

static inline void *dlopen(const char *path, int flags) {
    (void)flags;
    return (void *)LoadLibraryA(path);
}

static inline void *dlsym(void *handle, const char *symbol) {
    return (void *)(uintptr_t)GetProcAddress((HMODULE)handle, symbol);
}

static inline int dlclose(void *handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

static inline const char *dlerror(void) {
    static char buf[256];
    DWORD       err = GetLastError();
    if (!err) return NULL;
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
                             0, buf, sizeof(buf), NULL);
    if (n == 0) snprintf(buf, sizeof(buf), "error %lu", err);
    return buf;
}

#define FNM_NOMATCH  1
#define FNM_PATHNAME 0
#define FNM_NOESCAPE 0
#define FNM_PERIOD   0

static inline int mel__fnmatch(const char *p, const char *s) {
    while (*p) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 0;
            while (*s) {
                if (mel__fnmatch(p, s) == 0) return 0;
                s++;
            }
            return mel__fnmatch(p, s);
        } else if (*p == '?') {
            if (!*s) return 1;
            p++, s++;
        } else if (*p == '[') {
            const char *r   = p + 1;
            int         neg = (*r == '!' || *r == '^');
            if (neg) r++;
            int matched = 0, first = 1;
            while (*r && (*r != ']' || first)) {
                first = 0;
                if (r[1] == '-' && r[2] && r[2] != ']') {
                    if ((unsigned char)*s >= (unsigned char)r[0] && (unsigned char)*s <= (unsigned char)r[2])
                        matched = 1;
                    r += 3;
                } else {
                    if (*r == *s) matched = 1;
                    r++;
                }
            }
            if (*r != ']') return 1;
            if (!*s || matched == neg) return 1;
            p = r + 1, s++;
        } else if (*p == '\\') {
            p++;
            if (*p != *s) return 1;
            p++, s++;
        } else {
            if (*p != *s) return 1;
            p++, s++;
        }
    }
    return *s ? 1 : 0;
}

static inline int fnmatch(const char *pattern, const char *string, int flags) {
    (void)flags;
    return mel__fnmatch(pattern, string);
}

#endif

#endif
