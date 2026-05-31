#ifndef MEL_BUILD_INTERNAL_H
#define MEL_BUILD_INTERNAL_H

#include "build.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MEL_VEC(T) \
    struct {       \
        T     *items; \
        size_t len;   \
        size_t cap;   \
    }

void mel__grow(void **items, size_t *cap, size_t elem);

#define mel_da_push(v, x)                                                  \
    do {                                                                   \
        if ((v)->len == (v)->cap)                                          \
            mel__grow((void **)&(v)->items, &(v)->cap, sizeof *(v)->items); \
        (v)->items[(v)->len++] = (x);                                      \
    } while (0)

typedef struct {
    Mel_When    when;
    const char *glob;
} Mel_Glob;

typedef struct {
    Mel_When       when;
    Mel_Visibility vis;
    const char    *value;
} Mel_Flag;

typedef struct {
    const char *key;
    const char *value;
} Mel_KV;

typedef struct {
    const char *header;
} Mel_Codegen;

typedef struct {
    Mel_Stage           stage;
    Mel_Build_Stage_Fn  fn;
} Mel_Hook;

typedef MEL_VEC(const char *) Mel_StrVec;
typedef MEL_VEC(Mel_When)     Mel_WhenVec;
typedef MEL_VEC(Mel_Glob)     Mel_GlobVec;
typedef MEL_VEC(Mel_Flag)     Mel_FlagVec;
typedef MEL_VEC(Mel_KV)       Mel_KVVec;
typedef MEL_VEC(Mel_Codegen)  Mel_CodegenVec;
typedef MEL_VEC(Mel_Hook)     Mel_HookVec;
typedef MEL_VEC(Mel_Stage)    Mel_StageVec;

struct Mel_Build_Target {
    const char *name;
    const char *dir;
    Mel_Kind    kind;
    bool        kind_set;

    Mel_StrVec     deps;
    Mel_StrVec     host_deps;
    Mel_WhenVec    unavailable;
    Mel_GlobVec    sources;
    Mel_GlobVec    excludes;
    Mel_FlagVec    cflags;
    Mel_FlagVec    defines;
    Mel_FlagVec    includes;
    Mel_FlagVec    links;
    Mel_KVVec      manifest;
    Mel_CodegenVec codegens;
    Mel_HookVec    hooks;
    Mel_StageVec   suppressed;
};

#endif
