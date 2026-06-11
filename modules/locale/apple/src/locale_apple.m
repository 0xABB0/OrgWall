#include <locale/provider.h>
#include <log/log.h>

#import <Foundation/Foundation.h>

#include <allocator/allocator.h>

static u32 apple_enumerate(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap)
{
    (void)user;
    @autoreleasepool
    {
        NSArray<NSString*>* langs = [NSLocale preferredLanguages];
        u32                 count = (u32)langs.count;
        if (count > cap)
            return count;
        u32 produced = 0;
        for (NSString* lang in langs)
        {
            const char* utf8 = lang.UTF8String;
            if (!utf8)
                continue;
            usize len = strlen(utf8);
            u8*   buf = (u8*)mel_alloc(alloc, len);
            if (!buf)
                continue;
            memcpy(buf, utf8, len);
            out[produced++] = (Mel_Locale_Raw){ .tag = { .data = buf, .len = (size)len } };
        }
        return produced;
    }
}

static id g_observer;

static void apple_watch(void* user, Mel_Locale_Change_Notify notify, void* core)
{
    (void)user;
    if (g_observer)
        return;
    g_observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSCurrentLocaleDidChangeNotification
                                                                  object:nil
                                                                   queue:nil
                                                              usingBlock:^(NSNotification* note) {
                                                                  (void)note;
                                                                  notify(core);
                                                              }];
}

static void apple_unwatch(void* user)
{
    (void)user;
    if (g_observer)
    {
        [[NSNotificationCenter defaultCenter] removeObserver:g_observer];
        g_observer = nil;
    }
}

void mel_locale__register_host_providers(void)
{
    static const Mel_Locale_Provider_Desc desc = {
        .name = "apple-nslocale",
        .enumerate = apple_enumerate,
        .watch = apple_watch,
        .unwatch = apple_unwatch,
    };
    mel_locale_provider_register(&desc);
}
