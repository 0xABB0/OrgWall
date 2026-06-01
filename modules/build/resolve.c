#include "runner.h"

#include <stdio.h>

const char* mel_platform_name(Mel_Platform p)
{
    switch (p)
    {
    case MEL_PLATFORM_MACOS:
        return "macos";
    case MEL_PLATFORM_IOS:
        return "ios";
    case MEL_PLATFORM_LINUX:
        return "linux";
    case MEL_PLATFORM_ANDROID:
        return "android";
    case MEL_PLATFORM_WIN32:
        return "win32";
    case MEL_PLATFORM_WASM:
        return "wasm";
    default:
        return "unknown";
    }
}

Mel_Variant mel_variant_native(Mel_Platform platform, const char* config)
{
    Mel_Variant v = { .platform = platform, .config = config ? config : "debug", .arch = "x86_64" };
    switch (platform)
    {
    case MEL_PLATFORM_MACOS:
        v.backend = "cocoa";
        v.gpu = "metal";
        v.arch = "arm64";
        break;
    case MEL_PLATFORM_IOS:
        v.backend = "uikit";
        v.gpu = "metal";
        v.arch = "arm64";
        v.simulator = true;
        break;
    case MEL_PLATFORM_LINUX:
        v.gpu = "vulkan";
        break;
    case MEL_PLATFORM_ANDROID:
        v.backend = "androidnative";
        v.gpu = "vulkan";
        v.arch = "arm64";
        break;
    case MEL_PLATFORM_WIN32:
        v.backend = "winui";
        v.gpu = "vulkan";
        break;
    case MEL_PLATFORM_WASM:
        v.backend = "dom";
        v.gpu = "webgpu";
        v.runtime = "emscripten";
        v.arch = "wasm32";
        break;
    default:
        break;
    }
    return v;
}

bool mel_target_available(Mel_Target* t, const Mel_Variant* v)
{
    for (size_t i = 0; i < t->unavailable.len; i++)
        if (mel_when_match(t->unavailable.items[i], v))
            return false;
    return true;
}

static void add_props(Mel_Target* t, const Mel_Variant* v, bool private_too, Mel_StrVec* out)
{
    for (size_t i = 0; i < t->includes.len; i++)
    {
        Mel_Flag f = t->includes.items[i];
        if (!private_too && f.vis != MEL_PUBLIC)
            continue;
        if (!mel_when_match(f.when, v))
            continue;
        bool  abs = f.value[0] == '/' || (f.value[0] && f.value[1] == ':');
        char* path = abs ? mel_str_dup(f.value) : mel_path_join(t->dir, f.value);
        mel_da_push(out, mel_str_fmt("-I%s", path));
        free(path);
    }
    for (size_t i = 0; i < t->defines.len; i++)
    {
        Mel_Flag f = t->defines.items[i];
        if (!private_too && f.vis != MEL_PUBLIC)
            continue;
        if (!mel_when_match(f.when, v))
            continue;
        mel_da_push(out, mel_str_fmt("-D%s", f.value));
    }
    for (size_t i = 0; i < t->cflags.len; i++)
    {
        Mel_Flag f = t->cflags.items[i];
        if (!private_too && f.vis != MEL_PUBLIC)
            continue;
        if (!mel_when_match(f.when, v))
            continue;
        mel_da_push(out, mel_str_dup(f.value));
    }
}

bool mel_gather_compile(Mel_Graph* g, size_t idx, const Mel_Variant* v, Mel_StrVec* srcs, Mel_StrVec* cflags)
{
    Mel_Target* target = g->nodes.items[idx].t;

    for (size_t i = 0; i < target->sources.len; i++)
    {
        Mel_Glob s = target->sources.items[i];
        if (mel_when_match(s.when, v))
            mel_glob(target->dir, s.glob, srcs);
    }
    for (size_t i = 0; i < target->excludes.len; i++)
    {
        Mel_Glob s = target->excludes.items[i];
        if (!mel_when_match(s.when, v))
            continue;
        Mel_StrVec drop = { 0 };
        mel_glob(target->dir, s.glob, &drop);
        for (size_t k = 0; k < srcs->len;)
        {
            bool hit = false;
            for (size_t j = 0; j < drop.len; j++)
                if (strcmp(srcs->items[k], drop.items[j]) == 0)
                    hit = true;
            if (hit)
            {
                free((void*)srcs->items[k]);
                srcs->items[k] = srcs->items[--srcs->len];
            }
            else
                k++;
        }
        for (size_t k = 0; k < drop.len; k++)
            free((void*)drop.items[k]);
        free(drop.items);
    }

    Mel_IdxVec order = { 0 };
    if (!mel_topo_closure(g, target->name, &order))
    {
        free(order.items);
        return false;
    }
    for (size_t i = 0; i < order.len; i++)
    {
        size_t      di = order.items[i];
        Mel_Target* d = g->nodes.items[di].t;
        if (!mel_target_available(d, v))
        {
            fprintf(stderr, "build: '%s' (needed by '%s') is unavailable on %s\n", d->name, target->name, mel_platform_name(v->platform));
            free(order.items);
            return false;
        }
        add_props(d, v, di == idx, cflags);
    }
    free(order.items);
    return true;
}

void mel_gather_link(Mel_Graph* g, size_t idx, const Mel_Variant* v, Mel_StrVec* ldflags)
{
    Mel_Target* target = g->nodes.items[idx].t;
    Mel_IdxVec  order = { 0 };
    if (!mel_topo_closure(g, target->name, &order))
    {
        free(order.items);
        return;
    }
    for (size_t i = 0; i < order.len; i++)
    {
        size_t      di = order.items[i];
        Mel_Target* d = g->nodes.items[di].t;
        bool        own = di == idx;
        for (size_t k = 0; k < d->links.len; k++)
        {
            Mel_Flag f = d->links.items[k];
            if (!own && f.vis != MEL_PUBLIC)
                continue;
            if (!mel_when_match(f.when, v))
                continue;
            mel_da_push(ldflags, mel_str_dup(f.value));
        }
    }
    free(order.items);
}
