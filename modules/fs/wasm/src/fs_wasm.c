#include "../../src/posix/fs_posix_ops.inl"

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

static Mel_Fs_Path_Result dup_path(str8 s, const Mel_Alloc* alloc)
{
    Mel_Fs_Path_Result r = { 0 };
    r.value = str8_dup_alloc(s, alloc);
    r.status = MEL_FS_OK;
    return r;
}

static Mel_Fs_Path_Result unavailable(void)
{
    Mel_Fs_Path_Result r = { 0 };
    r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
    return r;
}

Mel_Fs_Path_Result mel_fs__backend_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc)
{
    switch (folder)
    {
    case MEL_FS_FOLDER_BASE:
        return dup_path(S8("/"), alloc);
    case MEL_FS_FOLDER_PREF:
    {
        str8 leaf = STR8_EMPTY;
        if (g_pref.set && g_pref.app.len > 0)
            leaf = g_pref.app;
        else if (g_pref.set && g_pref.bundle_id)
            leaf = str8_from_cstr(g_pref.bundle_id);
        if (leaf.len == 0)
            return dup_path(S8("/home/web_user"), alloc);
        u8   buf[1024];
        str8 joined = mel_path_join(S8("/home/web_user"), leaf, buf, sizeof buf);
        return dup_path(joined, alloc);
    }
    case MEL_FS_FOLDER_HOME:
        return dup_path(S8("/home/web_user"), alloc);
    case MEL_FS_FOLDER_TEMP:
    case MEL_FS_FOLDER_CACHE:
        return dup_path(S8("/tmp"), alloc);
    case MEL_FS_FOLDER_DESKTOP:
    case MEL_FS_FOLDER_DOCUMENTS:
    case MEL_FS_FOLDER_DOWNLOADS:
    case MEL_FS_FOLDER_MUSIC:
    case MEL_FS_FOLDER_PICTURES:
    case MEL_FS_FOLDER_VIDEOS:
    case MEL_FS_FOLDER_TEMPLATES:
    case MEL_FS_FOLDER_SAVED_GAMES:
    case MEL_FS_FOLDER_SCREENSHOTS:
    default:
        return unavailable();
    }
}
