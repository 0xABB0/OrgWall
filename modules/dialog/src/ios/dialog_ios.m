#include <dialog/backend.h>
#include <window/window.h>
#include <log/log.h>

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

bool mel_dialog__plat_available(void) { return true; }

@interface MelDialogPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, assign) u64 token;
@end

static NSMutableArray<MelDialogPickerDelegate*>* g_delegates;

@implementation MelDialogPickerDelegate

static void mel_dialog_ios_complete(u64 token, NSArray<NSURL*>* urls, Mel_Dialog_Status base, MelDialogPickerDelegate* self_ref)
{
    Mel_Dialog_Job* job = mel_dialog__job_from_token(token);
    if (job)
    {
        for (NSURL* u in urls)
        {
            BOOL        scoped = [u startAccessingSecurityScopedResource];
            const char* p = u.fileSystemRepresentation;
            if (p)
                mel_dialog_job_emit_path(job, p);
            if (scoped)
                [u stopAccessingSecurityScopedResource];
        }
        mel_dialog_job_resolve(job, base);
    }
    if (g_delegates)
        [g_delegates removeObject:self_ref];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    mel_dialog_ios_complete(self.token, urls, MEL_DIALOG_OK, self);
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    mel_dialog_ios_complete(self.token, @[], MEL_DIALOG_OK | MEL_DIALOG_CANCELLED, self);
}

@end

static NSArray<UTType*>* content_types(Mel_Dialog_Job* job, bool* out_ignored) API_AVAILABLE(ios(14.0))
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    u32                      fc = mel_dialog_job_filter_count(job);
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
                *out_ignored = true;
                continue;
            }
            UTType* t = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:ext]];
            if (t)
                [types addObject:t];
        }
    }
    if (types.count == 0)
        [types addObject:UTTypeItem];
    return types;
}

static UIViewController* root_for(Mel_Window parent)
{
    if (parent.index != 0)
    {
        void* native = mel_window_native(parent);
        if (native)
        {
            id obj = (__bridge id)native;
            if ([obj isKindOfClass:[UIWindow class]])
                return ((UIWindow*)obj).rootViewController;
            if ([obj isKindOfClass:[UIViewController class]])
                return (UIViewController*)obj;
        }
    }
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:[UIWindowScene class]])
            for (UIWindow* w in ((UIWindowScene*)scene).windows)
                if (w.isKeyWindow)
                    return w.rootViewController;
    return nil;
}

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    u64 token = mel_dialog_job_token(job);
    u32 request = mel_dialog_job_request(job);
    Mel_Window parent = mel_dialog_job_parent(job);

    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
            Mel_Dialog_Job* j = mel_dialog__job_from_token(token);
            if (!j)
                return;
            UIViewController* root = root_for(parent);
            if (!root)
            {
                mel_log_error("dialog", "ios: no presenting view controller");
                mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_BAD_PARENT);
                return;
            }

            if (@available(iOS 14.0, *))
            {
                UIDocumentPickerViewController* picker;
                if (request & MEL_DIALOG_REQUEST_OPEN_DIR)
                {
                    picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]];
                }
                else if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
                {
                    bool      ignored = false;
                    NSArray*  types = content_types(j, &ignored);
                    (void)types;
                    if (ignored)
                        mel_dialog_job_add_warning(j, MEL_DIALOG_WARN_FILTER_IGNORED);
                    mel_dialog_job_add_warning(j, MEL_DIALOG_WARN_SAVE_UNSUPPORTED);
                    NSString* name = mel_dialog_job_default_name(j) ? [NSString stringWithUTF8String:mel_dialog_job_default_name(j)] : @"untitled";
                    NSURL*    tmp = [[NSFileManager.defaultManager temporaryDirectory] URLByAppendingPathComponent:name];
                    [@"" writeToURL:tmp atomically:YES encoding:NSUTF8StringEncoding error:nil];
                    picker = [[UIDocumentPickerViewController alloc] initForExportingURLs:@[ tmp ] asCopy:YES];
                }
                else
                {
                    bool     ignored = false;
                    NSArray* types = content_types(j, &ignored);
                    if (ignored)
                        mel_dialog_job_add_warning(j, MEL_DIALOG_WARN_FILTER_IGNORED);
                    picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types];
                    picker.allowsMultipleSelection = (request & MEL_DIALOG_REQUEST_MULTI) ? YES : NO;
                }

                MelDialogPickerDelegate* del = [[MelDialogPickerDelegate alloc] init];
                del.token = token;
                picker.delegate = del;
                if (!g_delegates)
                    g_delegates = [NSMutableArray array];
                [g_delegates addObject:del];

                const char* title = mel_dialog_job_title(j);
                if (title)
                    picker.title = [NSString stringWithUTF8String:title];
                [root presentViewController:picker animated:YES completion:nil];
            }
            else
            {
                mel_log_error("dialog", "ios: UIDocumentPicker requires iOS 14");
                mel_dialog_job_resolve(j, MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE);
            }
        }
    });
}
