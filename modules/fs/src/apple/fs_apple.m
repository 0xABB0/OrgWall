#include "../posix/fs_posix_ops.inl"

#import <Foundation/Foundation.h>

#include <allocator/allocator.h>
#include <string/str8.h>
#include <string/path.h>

typedef struct
{
    str8        org;
    str8        app;
    const char* bundle_id;
    bool        set;
} Fs_Pref_Identity;

static Fs_Pref_Identity g_pref;

void mel_fs_pref_identity_opt(Mel_Fs_Pref_Opt opt)
{
    g_pref.org = opt.org;
    g_pref.app = opt.app;
    g_pref.bundle_id = opt.bundle_id;
    g_pref.set = true;
}

static Mel_Fs_Path_Result path_from_nsstring(NSString* s, const Mel_Alloc* alloc)
{
    Mel_Fs_Path_Result r = { 0 };
    if (!s)
    {
        r.status = MEL_FS_ERROR | MEL_FS_NOT_FOUND;
        return r;
    }
    const char* c = [s fileSystemRepresentation];
    r.value = str8_dup_alloc(str8_from_cstr(c), alloc);
    r.status = MEL_FS_OK;
    return r;
}

static Mel_Fs_Path_Result first_search_path(NSSearchPathDirectory dir, const Mel_Alloc* alloc)
{
    NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(dir, NSUserDomainMask, YES);
    if (paths.count == 0)
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    return path_from_nsstring(paths[0], alloc);
}

static Mel_Fs_Path_Result home_subdir(const char* sub, const Mel_Alloc* alloc)
{
    NSString* home = NSHomeDirectory();
    NSString* p = sub ? [home stringByAppendingPathComponent:[NSString stringWithUTF8String:sub]] : home;
    return path_from_nsstring(p, alloc);
}

Mel_Fs_Path_Result mel_fs__backend_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc)
{
    @autoreleasepool
    {
        switch (folder)
        {
        case MEL_FS_FOLDER_BASE:
        {
            NSBundle* b = [NSBundle mainBundle];
            NSString* rp = [b resourcePath];
            if (rp)
                return path_from_nsstring(rp, alloc);
            return mel_fs__backend_cwd(alloc);
        }
        case MEL_FS_FOLDER_PREF:
        {
            Mel_Fs_Path_Result app_support = first_search_path(NSApplicationSupportDirectory, alloc);
            if (mel_fs_failed(app_support.status))
                return app_support;
            const char* leaf = NULL;
            if (g_pref.set && g_pref.bundle_id)
                leaf = g_pref.bundle_id;
            else
            {
                NSString* bid = [[NSBundle mainBundle] bundleIdentifier];
                if (bid)
                    leaf = [bid UTF8String];
            }
            if (!leaf && g_pref.set && g_pref.app.len > 0)
            {
                Mel_Fs_Path_Result r = { 0 };
                u8                 buf[2048];
                str8               joined = mel_path_join(app_support.value, g_pref.app, buf, sizeof buf);
                r.value = str8_dup_alloc(joined, alloc);
                r.status = MEL_FS_OK;
                mel_dealloc(alloc, app_support.value.data);
                return r;
            }
            if (!leaf)
                return app_support;
            u8                 buf[2048];
            str8               joined = mel_path_join(app_support.value, str8_from_cstr(leaf), buf, sizeof buf);
            Mel_Fs_Path_Result r = { 0 };
            r.value = str8_dup_alloc(joined, alloc);
            r.status = MEL_FS_OK;
            mel_dealloc(alloc, app_support.value.data);
            return r;
        }
        case MEL_FS_FOLDER_HOME:
            return home_subdir(NULL, alloc);
        case MEL_FS_FOLDER_DESKTOP:
            return first_search_path(NSDesktopDirectory, alloc);
        case MEL_FS_FOLDER_DOCUMENTS:
            return first_search_path(NSDocumentDirectory, alloc);
        case MEL_FS_FOLDER_DOWNLOADS:
            return first_search_path(NSDownloadsDirectory, alloc);
        case MEL_FS_FOLDER_MUSIC:
            return first_search_path(NSMusicDirectory, alloc);
        case MEL_FS_FOLDER_PICTURES:
            return first_search_path(NSPicturesDirectory, alloc);
        case MEL_FS_FOLDER_VIDEOS:
            return first_search_path(NSMoviesDirectory, alloc);
        case MEL_FS_FOLDER_TEMPLATES:
            return home_subdir("Templates", alloc);
        case MEL_FS_FOLDER_SAVED_GAMES:
            return home_subdir("Library/Application Support/Saved Games", alloc);
        case MEL_FS_FOLDER_SCREENSHOTS:
            return first_search_path(NSPicturesDirectory, alloc);
        case MEL_FS_FOLDER_CACHE:
            return first_search_path(NSCachesDirectory, alloc);
        case MEL_FS_FOLDER_TEMP:
            return path_from_nsstring(NSTemporaryDirectory(), alloc);
        default:
        {
            Mel_Fs_Path_Result r = { 0 };
            r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
            return r;
        }
        }
    }
}
