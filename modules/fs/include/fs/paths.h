#pragma once

#include <fs/fs.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Fs_Folder;

#define MEL_FS_FOLDER_BASE        0u
#define MEL_FS_FOLDER_PREF        1u
#define MEL_FS_FOLDER_HOME        2u
#define MEL_FS_FOLDER_DESKTOP     3u
#define MEL_FS_FOLDER_DOCUMENTS   4u
#define MEL_FS_FOLDER_DOWNLOADS   5u
#define MEL_FS_FOLDER_MUSIC       6u
#define MEL_FS_FOLDER_PICTURES    7u
#define MEL_FS_FOLDER_VIDEOS      8u
#define MEL_FS_FOLDER_TEMPLATES   9u
#define MEL_FS_FOLDER_SAVED_GAMES 10u
#define MEL_FS_FOLDER_SCREENSHOTS 11u
#define MEL_FS_FOLDER_CACHE       12u
#define MEL_FS_FOLDER_TEMP        13u
#define MEL_FS_FOLDER_COUNT       14u

typedef struct
{
    str8        org;
    str8        app;
    const char* bundle_id;
} Mel_Fs_Pref_Opt;

void mel_fs_pref_identity_opt(Mel_Fs_Pref_Opt opt);
#define mel_fs_pref_identity(...) mel_fs_pref_identity_opt((Mel_Fs_Pref_Opt){ __VA_ARGS__ })

Mel_Fs_Path_Result mel_fs_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc);

Mel_Fs_Path_Result mel_fs_cwd(const Mel_Alloc* alloc);
Mel_Fs_Void_Result mel_fs_chdir(str8 path);

#ifdef __cplusplus
}
#endif
