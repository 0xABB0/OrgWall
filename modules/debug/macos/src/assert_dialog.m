#include "../../src/assert_backend.h"

#include <debug/stacktrace.h>

#include <allocator/heap.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

bool mel__assert_dialog_available(void) { return NSApp != nil; }

Mel_Assert_Response mel__assert_dialog(const Mel_Assert_Report* report)
{
    if (NSApp == nil)
        return mel_assert_default_handler(report, NULL);

    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             trace = report->stack != NULL ? mel_stacktrace_format(report->stack, (Mel_Alloc*)alloc) : STR8_EMPTY;

    __block Mel_Assert_Response response = MEL_ASSERT_RESPONSE_ABORT;

    void (^show)(void) = ^{
        @autoreleasepool
        {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.alertStyle = NSAlertStyleCritical;
            alert.messageText = [NSString stringWithFormat:@"Assertion failed: %.*s", (int)report->condition.len, report->condition.data];

            NSMutableString* info = [NSMutableString string];
            [info appendFormat:@"%.*s", (int)report->location.len, report->location.data];
            if (report->message.len > 0)
                [info appendFormat:@"\n%.*s", (int)report->message.len, report->message.data];
            if (trace.len > 0)
                [info appendFormat:@"\n\n%.*s", (int)trace.len, trace.data];
            alert.informativeText = info;

            [alert addButtonWithTitle:@"Abort"];
            [alert addButtonWithTitle:@"Retry"];
            [alert addButtonWithTitle:@"Ignore Once"];
            [alert addButtonWithTitle:@"Ignore Forever"];

            NSModalResponse r = [alert runModal];
            switch (r)
            {
            case NSAlertSecondButtonReturn:
                response = MEL_ASSERT_RESPONSE_RETRY;
                break;
            case NSAlertThirdButtonReturn:
                response = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
                break;
            case NSAlertThirdButtonReturn + 1:
                response = MEL_ASSERT_RESPONSE_IGNORE_FOREVER;
                break;
            default:
                response = MEL_ASSERT_RESPONSE_BREAK | MEL_ASSERT_RESPONSE_ABORT;
                break;
            }
        }
    };

    if ([NSThread isMainThread])
        show();
    else
        dispatch_sync(dispatch_get_main_queue(), show);

    if (trace.data != NULL)
        mel_dealloc(alloc, trace.data);

    return response;
}
