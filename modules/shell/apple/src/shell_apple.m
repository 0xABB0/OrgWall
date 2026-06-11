#include <shell/backend.h>
#include <log/log.h>

#import <TargetConditionals.h>
#import <Foundation/Foundation.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

bool mel_shell__plat_available(void) { return true; }

static NSURL* url_for_target(str8 t)
{
    NSString* s = [[NSString alloc] initWithBytes:t.data length:(NSUInteger)t.len encoding:NSUTF8StringEncoding];
    if (!s)
        return nil;
    NSURL* u = [NSURL URLWithString:s];
    if (u && u.scheme)
        return u;
    return [NSURL fileURLWithPath:s];
}

static NSURL* file_url_for_target(str8 t)
{
    NSString* s = [[NSString alloc] initWithBytes:t.data length:(NSUInteger)t.len encoding:NSUTF8StringEncoding];
    if (!s)
        return nil;
    if ([s hasPrefix:@"file://"])
    {
        NSURL* u = [NSURL URLWithString:s];
        return u ? u : [NSURL fileURLWithPath:s];
    }
    return [NSURL fileURLWithPath:s];
}

#if TARGET_OS_OSX

void mel_shell__plat_open_url(Mel_Shell_Job* job)
{
    @autoreleasepool
    {
        NSURL* u = url_for_target(mel_shell_job_target(job));
        if (!u)
        {
            mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
            return;
        }
        u64                           tok = mel_shell_job_token(job);
        NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
        [[NSWorkspace sharedWorkspace] openURL:u
                                 configuration:cfg
                             completionHandler:^(NSRunningApplication* app, NSError* err) {
                                 dispatch_async(dispatch_get_main_queue(), ^{
                                     Mel_Shell_Job* j = mel_shell__job_from_token(tok);
                                     if (!j)
                                         return;
                                     if (err || !app)
                                         mel_shell_job_resolve(j, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER);
                                     else
                                         mel_shell_job_resolve(j, MEL_SHELL_OK);
                                 });
                             }];
    }
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* job)
{
    @autoreleasepool
    {
        NSURL* u = file_url_for_target(mel_shell_job_target(job));
        if (!u)
        {
            mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
            return;
        }
        if (![u checkResourceIsReachableAndReturnError:nil])
        {
            mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NOT_FOUND);
            return;
        }
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ u ]];
        mel_shell_job_resolve(job, MEL_SHELL_OK);
    }
}

void* mel_shell__plat_native(void) { return (__bridge void*)[NSWorkspace sharedWorkspace]; }

#else

void mel_shell__plat_open_url(Mel_Shell_Job* job)
{
    @autoreleasepool
    {
        NSURL* u = url_for_target(mel_shell_job_target(job));
        if (!u)
        {
            mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
            return;
        }
        UIApplication* app = [UIApplication sharedApplication];
        if (![app canOpenURL:u])
        {
            mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER);
            return;
        }
        u64 tok = mel_shell_job_token(job);
        [app openURL:u
            options:@{}
            completionHandler:^(BOOL ok) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    Mel_Shell_Job* j = mel_shell__job_from_token(tok);
                    if (!j)
                        return;
                    mel_shell_job_resolve(j, ok ? MEL_SHELL_OK : (MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER));
                });
            }];
    }
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* job)
{
    mel_log_warn("shell", "reveal_path: iOS has no user-visible file manager for arbitrary paths");
    mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER);
}

void* mel_shell__plat_native(void) { return (__bridge void*)[UIApplication sharedApplication]; }

#endif
