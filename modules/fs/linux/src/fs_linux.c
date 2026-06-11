#include "../../src/posix/fs_posix_ops.inl"

#include <allocator/allocator.h>
#include <string/str8.h>
#include <string/path.h>

#include <stdlib.h>

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

static str8 env_str8(const char* name)
{
    const char* v = getenv(name);
    return v && v[0] ? str8_from_cstr(v) : STR8_EMPTY;
}

static Mel_Fs_Path_Result join_home(const char* sub, const Mel_Alloc* alloc)
{
    str8 home = env_str8("HOME");
    if (home.len == 0)
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    u8   buf[2048];
    str8 joined = sub ? mel_path_join(home, str8_from_cstr(sub), buf, sizeof buf) : home;
    return dup_path(joined, alloc);
}

static Mel_Fs_Path_Result xdg_user_dir(const char* xdg_env, const char* fallback_sub, const Mel_Alloc* alloc)
{
    str8 v = env_str8(xdg_env);
    if (v.len > 0)
        return dup_path(v, alloc);
    return join_home(fallback_sub, alloc);
}

Mel_Fs_Path_Result mel_fs__backend_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc)
{
    switch (folder)
    {
    case MEL_FS_FOLDER_BASE:
    {
        char    buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0)
        {
            buf[n] = '\0';
            str8 exe = str8_from_parts((u8*)buf, (size)n);
            return dup_path(mel_path_parent(exe), alloc);
        }
        return mel_fs__backend_cwd(alloc);
    }
    case MEL_FS_FOLDER_PREF:
    {
        str8 data = env_str8("XDG_DATA_HOME");
        u8   base_buf[2048];
        str8 base;
        if (data.len > 0)
            base = data;
        else
        {
            str8 home = env_str8("HOME");
            if (home.len == 0)
            {
                Mel_Fs_Path_Result r = { 0 };
                r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
                return r;
            }
            base = mel_path_join(home, S8(".local/share"), base_buf, sizeof base_buf);
        }
        str8 leaf = STR8_EMPTY;
        if (g_pref.set && g_pref.app.len > 0)
            leaf = g_pref.app;
        else if (g_pref.set && g_pref.bundle_id)
            leaf = str8_from_cstr(g_pref.bundle_id);
        if (leaf.len == 0)
            return dup_path(base, alloc);
        u8   buf[2048];
        str8 joined = mel_path_join(base, leaf, buf, sizeof buf);
        return dup_path(joined, alloc);
    }
    case MEL_FS_FOLDER_HOME:
        return join_home(NULL, alloc);
    case MEL_FS_FOLDER_DESKTOP:
        return xdg_user_dir("XDG_DESKTOP_DIR", "Desktop", alloc);
    case MEL_FS_FOLDER_DOCUMENTS:
        return xdg_user_dir("XDG_DOCUMENTS_DIR", "Documents", alloc);
    case MEL_FS_FOLDER_DOWNLOADS:
        return xdg_user_dir("XDG_DOWNLOAD_DIR", "Downloads", alloc);
    case MEL_FS_FOLDER_MUSIC:
        return xdg_user_dir("XDG_MUSIC_DIR", "Music", alloc);
    case MEL_FS_FOLDER_PICTURES:
        return xdg_user_dir("XDG_PICTURES_DIR", "Pictures", alloc);
    case MEL_FS_FOLDER_VIDEOS:
        return xdg_user_dir("XDG_VIDEOS_DIR", "Videos", alloc);
    case MEL_FS_FOLDER_TEMPLATES:
        return xdg_user_dir("XDG_TEMPLATES_DIR", "Templates", alloc);
    case MEL_FS_FOLDER_SAVED_GAMES:
    {
        str8 data = env_str8("XDG_DATA_HOME");
        if (data.len > 0)
        {
            u8   buf[2048];
            str8 joined = mel_path_join(data, S8("Saved Games"), buf, sizeof buf);
            return dup_path(joined, alloc);
        }
        return join_home(".local/share/Saved Games", alloc);
    }
    case MEL_FS_FOLDER_SCREENSHOTS:
        return xdg_user_dir("XDG_PICTURES_DIR", "Pictures/Screenshots", alloc);
    case MEL_FS_FOLDER_CACHE:
    {
        str8 cache = env_str8("XDG_CACHE_HOME");
        if (cache.len > 0)
            return dup_path(cache, alloc);
        return join_home(".cache", alloc);
    }
    case MEL_FS_FOLDER_TEMP:
    {
        str8 tmp = env_str8("TMPDIR");
        return dup_path(tmp.len > 0 ? tmp : S8("/tmp"), alloc);
    }
    default:
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    }
}
