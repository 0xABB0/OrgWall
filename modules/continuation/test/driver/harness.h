#pragma once

#include <core/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    i64* items;
    int  count;
    int  cap;
} Trace;

static inline void trace_push(Trace* t, i64 v)
{
    if (t->count == t->cap)
    {
        t->cap   = t->cap ? t->cap * 2 : 16;
        t->items = realloc(t->items, (size_t)t->cap * sizeof *t->items);
    }
    t->items[t->count++] = v;
}

static inline void trace_free(Trace* t)
{
    free(t->items);
    t->items = NULL;
    t->count = 0;
    t->cap   = 0;
}

static int          g_fail;
static const char*  g_case;

static inline void check_traces(const char* label, const Trace* expected, const Trace* got)
{
    if (expected->count != got->count)
    {
        g_fail++;
        fprintf(stderr, "[%s/%s] yield count: expected %d, got %d\n", g_case, label, expected->count, got->count);
        return;
    }
    for (int i = 0; i < expected->count; i++)
    {
        if (expected->items[i] != got->items[i])
        {
            g_fail++;
            fprintf(stderr, "[%s/%s] yield %d: expected %lld, got %lld\n", g_case, label, i, (long long)expected->items[i], (long long)got->items[i]);
            return;
        }
    }
}

static inline void check_eq_i64(const char* label, i64 expected, i64 got)
{
    if (expected != got)
    {
        g_fail++;
        fprintf(stderr, "[%s/%s] expected %lld, got %lld\n", g_case, label, (long long)expected, (long long)got);
    }
}

static inline int harness_report(const char* name)
{
    if (g_fail)
    {
        fprintf(stderr, "FAIL %s (%d checks failed)\n", name, g_fail);
        return 1;
    }
    fprintf(stderr, "ok %s\n", name);
    return 0;
}
