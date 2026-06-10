#include "boot_internal.h"

#include <boot/boot.h>
#include <collection/array.h>
#include <debug/assert.h>

typedef struct
{
    void (*fn)(void* user);
    void* user;
} Boot_Exit_Hook;

typedef struct
{
    int    argc;
    char** argv;
    int    exit_code;
    bool   live;
    Mel_Array(Boot_Exit_Hook) exits;
} Boot_State;

static Boot_State g_boot;

void mel_boot__init(int argc, char** argv, const Mel_Alloc* alloc)
{
    mel_assert(!g_boot.live);
    g_boot.argc = argc;
    g_boot.argv = argv;
    g_boot.exit_code = 0;
    g_boot.live = true;
    mel_array_init(&g_boot.exits, alloc);
}

int mel_boot__finish(void)
{
    mel_assert(g_boot.live);
    while (g_boot.exits.count > 0)
    {
        Boot_Exit_Hook hook = mel_array_pop(&g_boot.exits);
        hook.fn(hook.user);
    }
    mel_array_free(&g_boot.exits);
    g_boot.live = false;
    return g_boot.exit_code;
}

int mel_app_argc(void)
{
    mel_assert(g_boot.live);
    return g_boot.argc;
}

char** mel_app_argv(void)
{
    mel_assert(g_boot.live);
    return g_boot.argv;
}

void mel_app_set_exit_code(int code)
{
    mel_assert(g_boot.live);
    g_boot.exit_code = code;
}

void mel_app_on_exit(void (*fn)(void* user), void* user)
{
    mel_assert(g_boot.live);
    mel_assert(fn != NULL);
    Boot_Exit_Hook hook = { fn, user };
    mel_array_push(&g_boot.exits, hook);
}
