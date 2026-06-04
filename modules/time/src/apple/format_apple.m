#include <time/format_provider.h>

#import <Foundation/Foundation.h>

#include <string.h>

static u32 order_from_template(NSString* tmpl)
{
    NSRange y = [tmpl rangeOfString:@"y" options:NSCaseInsensitiveSearch];
    NSRange m = [tmpl rangeOfString:@"M"];
    NSRange d = [tmpl rangeOfString:@"d" options:NSCaseInsensitiveSearch];
    if (y.location == NSNotFound || m.location == NSNotFound || d.location == NSNotFound)
        return 0;
    if (y.location < m.location && y.location < d.location)
        return MEL_DATE_ORDER_YMD;
    if (d.location < m.location)
        return MEL_DATE_ORDER_DMY;
    return MEL_DATE_ORDER_MDY;
}

static void separator_from_template(NSString* tmpl, char out[4])
{
    out[0] = '\0';
    NSCharacterSet* fields = [NSCharacterSet characterSetWithCharactersInString:@"yYmMdDeE"];
    for (NSUInteger i = 0; i < tmpl.length; i++)
    {
        unichar c = [tmpl characterAtIndex:i];
        if ([fields characterIsMember:c] || c == ' ')
            continue;
        if (c < 128)
        {
            out[0] = (char)c;
            out[1] = '\0';
        }
        break;
    }
}

static bool apple_query(void* user, Mel_Time_Format_Prefs* out)
{
    (void)user;
    @autoreleasepool
    {
        NSLocale* loc = [NSLocale currentLocale];

        NSString* dt = [NSDateFormatter dateFormatFromTemplate:@"yMd" options:0 locale:loc];
        if (!dt)
            return false;
        u32 order = order_from_template(dt);
        if (order == 0)
            return false;

        NSString* clockTmpl = [NSDateFormatter dateFormatFromTemplate:@"j" options:0 locale:loc];
        bool      is12 = clockTmpl != nil && ([clockTmpl rangeOfString:@"a"].location != NSNotFound || [clockTmpl rangeOfString:@"h"].location != NSNotFound);

        memset(out, 0, sizeof *out);
        out->date_order = order;
        out->clock = is12 ? MEL_CLOCK_12H : MEL_CLOCK_24H;
        separator_from_template(dt, out->date_separator);
        if (out->date_separator[0] == '\0')
        {
            out->date_separator[0] = '/';
            out->date_separator[1] = '\0';
        }
        return true;
    }
}

void mel_time_format__register_host_providers(void)
{
    static const Mel_Time_Format_Provider_Desc desc = {
        .name = "apple-nsdateformatter",
        .query = apple_query,
    };
    mel_time_format_provider_register(&desc);
}
