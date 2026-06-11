#include <dialog/backend.h>
#include <window/window.h>
#include <log/log.h>

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

bool mel_dialog__plat_available(void) { return true; }

static void apply_filters(NSSavePanel* panel, Mel_Dialog_Job* job)
{
    u32 fc = mel_dialog_job_filter_count(job);
    if (fc == 0)
        return;

    if (@available(macOS 11.0, *))
    {
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        bool                     any_wildcard = false;
        for (u32 i = 0; i < fc; i++)
        {
            u32 pc = mel_dialog_job_filter_pattern_count(job, i);
            for (u32 p = 0; p < pc; p++)
            {
                const char* pat = mel_dialog_job_filter_pattern(job, i, p);
                if (!pat)
                    continue;
                const char* ext = pat;
                const char* dot = strrchr(pat, '.');
                if (dot)
                    ext = dot + 1;
                if (strcmp(ext, "*") == 0 || strchr(ext, '*'))
                {
                    any_wildcard = true;
                    continue;
                }
                NSString* e = [NSString stringWithUTF8String:ext];
                UTType*   t = [UTType typeWithFilenameExtension:e];
                if (t)
                    [types addObject:t];
            }
        }
        if (any_wildcard || types.count == 0)
        {
            mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_FILTER_IGNORED);
            return;
        }
        panel.allowedContentTypes = types;
        return;
    }

    NSMutableArray<NSString*>* exts = [NSMutableArray array];
    for (u32 i = 0; i < fc; i++)
    {
        u32 pc = mel_dialog_job_filter_pattern_count(job, i);
        for (u32 p = 0; p < pc; p++)
        {
            const char* pat = mel_dialog_job_filter_pattern(job, i, p);
            if (!pat)
                continue;
            const char* ext = pat;
            const char* dot = strrchr(pat, '.');
            if (dot)
                ext = dot + 1;
            if (strcmp(ext, "*") == 0 || strchr(ext, '*'))
            {
                mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_FILTER_IGNORED);
                continue;
            }
            [exts addObject:[NSString stringWithUTF8String:ext]];
        }
    }
    if (exts.count > 0)
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        panel.allowedFileTypes = exts;
#pragma clang diagnostic pop
    }
}

static void apply_default_path(NSSavePanel* panel, const char* dir, const char* name)
{
    if (dir)
    {
        NSString* d = [NSString stringWithUTF8String:dir];
        NSURL*    u = [NSURL fileURLWithPath:d isDirectory:YES];
        if (u)
            panel.directoryURL = u;
    }
    if (name)
        panel.nameFieldStringValue = [NSString stringWithUTF8String:name];
}

static void finish(u64 token, NSArray<NSURL*>* urls, Mel_Dialog_Status base)
{
    Mel_Dialog_Job* job = mel_dialog__job_from_token(token);
    if (!job)
        return;
    for (NSURL* u in urls)
    {
        const char* p = u.fileSystemRepresentation;
        if (p)
            mel_dialog_job_emit_path(job, p);
    }
    mel_dialog_job_resolve(job, base);
}

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    u64 token = mel_dialog_job_token(job);
    u32 request = mel_dialog_job_request(job);
    if (mel_dialog_job_parent(job).index != 0)
        mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_PARENT_IGNORED);

    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
            Mel_Dialog_Job* j = mel_dialog__job_from_token(token);
            if (!j)
                return;

            const char* title = mel_dialog_job_title(j);
            const char* dir = mel_dialog_job_default_path(j);

            if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
            {
                NSSavePanel* panel = [NSSavePanel savePanel];
                if (title)
                    panel.title = [NSString stringWithUTF8String:title];
                apply_filters(panel, j);
                apply_default_path(panel, dir, mel_dialog_job_default_name(j));
                NSModalResponse resp = [panel runModal];
                if (resp == NSModalResponseOK && panel.URL)
                    finish(token, @[ panel.URL ], MEL_DIALOG_OK);
                else
                    finish(token, @[], MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
                return;
            }

            NSOpenPanel* panel = [NSOpenPanel openPanel];
            if (title)
                panel.title = [NSString stringWithUTF8String:title];
            if (request & MEL_DIALOG_REQUEST_OPEN_DIR)
            {
                panel.canChooseDirectories = YES;
                panel.canChooseFiles = NO;
            }
            else
            {
                panel.canChooseDirectories = NO;
                panel.canChooseFiles = YES;
                apply_filters(panel, j);
            }
            panel.allowsMultipleSelection = (request & MEL_DIALOG_REQUEST_MULTI) ? YES : NO;
            apply_default_path(panel, dir, NULL);

            NSModalResponse resp = [panel runModal];
            if (resp == NSModalResponseOK && panel.URLs.count > 0)
                finish(token, panel.URLs, MEL_DIALOG_OK);
            else
                finish(token, @[], MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
        }
    });
}
