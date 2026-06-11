#include <clipboard/backend.h>
#include <log/log.h>

#import <TargetConditionals.h>
#import <Foundation/Foundation.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

static const struct
{
    Mel_Clip_Format f;
    const char*     uti;
} UTI_MAP[] = {
    { MEL_CLIP_FMT_TEXT, "public.utf8-plain-text" }, { MEL_CLIP_FMT_HTML, "public.html" }, { MEL_CLIP_FMT_PNG, "public.png" }, { MEL_CLIP_FMT_URI_LIST, "public.file-url" }, { MEL_CLIP_FMT_RTF, "public.rtf" },
};

static NSString* uti_for_format(Mel_Clip_Format f)
{
    for (usize i = 0; i < sizeof UTI_MAP / sizeof UTI_MAP[0]; i++)
        if (UTI_MAP[i].f == f)
            return [NSString stringWithUTF8String:UTI_MAP[i].uti];
    str8 mime = mel_clip_format_mime(f);
    if (str8_is_empty(mime))
        return nil;
    return [[NSString alloc] initWithBytes:mime.data length:(NSUInteger)mime.len encoding:NSUTF8StringEncoding];
}

static Mel_Clip_Format format_for_uti(NSString* uti)
{
    const char* c = uti.UTF8String;
    for (usize i = 0; i < sizeof UTI_MAP / sizeof UTI_MAP[0]; i++)
        if (c && strcmp(c, UTI_MAP[i].uti) == 0)
            return UTI_MAP[i].f;
    return mel_clip_format_register((str8){ (u8*)c, (size)(c ? strlen(c) : 0) });
}

bool mel_clip__plat_available(void) { return true; }

bool mel_clip__plat_channel_supported(Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_CLIPBOARD; }

void mel_clip__plat_shutdown(void) {}

#if TARGET_OS_OSX

u64 mel_clip__plat_sequence(Mel_Clip_Channel ch) { return mel_clip__plat_channel_supported(ch) ? (u64)[NSPasteboard generalPasteboard].changeCount : 0; }

void mel_clip__plat_read(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        NSPasteboard*   pb = [NSPasteboard generalPasteboard];
        u32             want = mel_clip_job_request_count(job);
        Mel_Clip_Status st = 0;
        u32             emitted = 0;

        if (want > 0)
        {
            for (u32 i = 0; i < want; i++)
            {
                Mel_Clip_Format f = mel_clip_job_request(job, i);
                NSString*       uti = uti_for_format(f);
                NSData*         d = uti ? [pb dataForType:uti] : nil;
                if (d)
                {
                    mel_clip_job_emit(job, f, d.bytes, d.length);
                    emitted++;
                }
                else
                    st |= MEL_CLIP_WARN_FORMAT_UNAVAILABLE;
            }
        }
        else
        {
            for (NSPasteboardType t in pb.types)
            {
                Mel_Clip_Format f = format_for_uti(t);
                NSData*         d = [pb dataForType:t];
                if (d && f != MEL_CLIP_FMT_NONE)
                {
                    mel_clip_job_emit(job, f, d.bytes, d.length);
                    emitted++;
                }
            }
        }
        if (emitted == 0)
            st |= MEL_CLIP_RESULT_EMPTY;
        mel_clip_job_resolve(job, (st & ~MEL_CLIP_SEVERITY_MASK) ? (st | MEL_CLIP_WARNED) : MEL_CLIP_OK);
    }
}

void mel_clip__plat_write(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSMutableArray* objs = [NSMutableArray array];
        u32             items = mel_clip_job_item_count(job);
        for (u32 i = 0; i < items; i++)
        {
            NSPasteboardItem* pi = [[NSPasteboardItem alloc] init];
            u32               reps = mel_clip_job_rep_count(job, i);
            for (u32 r = 0; r < reps; r++)
            {
                Mel_Clip_Rep rep = mel_clip_job_rep(job, i, r);
                NSString*    uti = uti_for_format(rep.format);
                NSData*      d = [NSData dataWithBytes:rep.bytes.data length:(NSUInteger)rep.bytes.len];
                if (!uti || ![pi setData:d forType:uti])
                    mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
            }
            [objs addObject:pi];
        }
        BOOL            ok = objs.count == 0 ? YES : [pb writeObjects:objs];
        Mel_Clip_Status base = ok ? MEL_CLIP_OK : MEL_CLIP_ERROR;
        mel_clip_job_resolve(job, base);
    }
}

void mel_clip__plat_clear(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        [[NSPasteboard generalPasteboard] clearContents];
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void mel_clip__plat_query(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        for (NSPasteboardType t in pb.types)
        {
            Mel_Clip_Format f = format_for_uti(t);
            if (f != MEL_CLIP_FMT_NONE)
                mel_clip_job_emit_format(job, f);
        }
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void mel_clip__plat_has(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        mel_clip_job_set_present(job, [NSPasteboard generalPasteboard].types.count > 0);
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void* mel_clip__plat_native(void) { return (__bridge void*)[NSPasteboard generalPasteboard]; }

#else

u64 mel_clip__plat_sequence(Mel_Clip_Channel ch) { return mel_clip__plat_channel_supported(ch) ? (u64)[UIPasteboard generalPasteboard].changeCount : 0; }

void mel_clip__plat_read(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        UIPasteboard*   pb = [UIPasteboard generalPasteboard];
        u32             want = mel_clip_job_request_count(job);
        Mel_Clip_Status st = 0;
        u32             emitted = 0;
        NSArray*        types = want > 0 ? nil : pb.pasteboardTypes;
        u32             n = want > 0 ? want : (u32)types.count;
        for (u32 i = 0; i < n; i++)
        {
            Mel_Clip_Format f;
            NSString*       uti;
            if (want > 0)
            {
                f = mel_clip_job_request(job, i);
                uti = uti_for_format(f);
            }
            else
            {
                uti = types[i];
                f = format_for_uti(uti);
            }
            NSData* d = (uti && f != MEL_CLIP_FMT_NONE) ? [pb dataForPasteboardType:uti] : nil;
            if (d)
            {
                mel_clip_job_emit(job, f, d.bytes, d.length);
                emitted++;
            }
            else if (want > 0)
                st |= MEL_CLIP_WARN_FORMAT_UNAVAILABLE;
        }
        if (emitted == 0)
            st |= MEL_CLIP_RESULT_EMPTY;
        mel_clip_job_resolve(job, (st & ~MEL_CLIP_SEVERITY_MASK) ? (st | MEL_CLIP_WARNED) : MEL_CLIP_OK);
    }
}

void mel_clip__plat_write(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        UIPasteboard*   pb = [UIPasteboard generalPasteboard];
        NSMutableArray* items = [NSMutableArray array];
        u32             nitems = mel_clip_job_item_count(job);
        for (u32 i = 0; i < nitems; i++)
        {
            NSMutableDictionary* dict = [NSMutableDictionary dictionary];
            u32                  reps = mel_clip_job_rep_count(job, i);
            for (u32 r = 0; r < reps; r++)
            {
                Mel_Clip_Rep rep = mel_clip_job_rep(job, i, r);
                NSString*    uti = uti_for_format(rep.format);
                if (uti)
                    dict[uti] = [NSData dataWithBytes:rep.bytes.data length:(NSUInteger)rep.bytes.len];
                else
                    mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
            }
            [items addObject:dict];
        }
        pb.items = items;
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void mel_clip__plat_clear(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        [UIPasteboard generalPasteboard].items = @[];
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void mel_clip__plat_query(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        for (NSString* t in [UIPasteboard generalPasteboard].pasteboardTypes)
        {
            Mel_Clip_Format f = format_for_uti(t);
            if (f != MEL_CLIP_FMT_NONE)
                mel_clip_job_emit_format(job, f);
        }
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void mel_clip__plat_has(Mel_Clip_Job* job)
{
    @autoreleasepool
    {
        mel_clip_job_set_present(job, [UIPasteboard generalPasteboard].pasteboardTypes.count > 0);
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
}

void* mel_clip__plat_native(void) { return (__bridge void*)[UIPasteboard generalPasteboard]; }

#endif
