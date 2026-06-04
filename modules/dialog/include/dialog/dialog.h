#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <window/window.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor  Mel_Reactor;
typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Future   Mel_Future;

typedef u32 Mel_Dialog_Status;

#define MEL_DIALOG_SEVERITY_MASK 0x3u
#define MEL_DIALOG_OK            0u
#define MEL_DIALOG_WARNED        1u
#define MEL_DIALOG_ERROR         2u

#define MEL_DIALOG_CANCELLED   (1u << 2)
#define MEL_DIALOG_NO_BACKEND  (1u << 3)
#define MEL_DIALOG_DENIED      (1u << 4)
#define MEL_DIALOG_BAD_PARENT  (1u << 5)
#define MEL_DIALOG_UNAVAILABLE (1u << 6)

#define MEL_DIALOG_WARN_FILTER_IGNORED       (1u << 8)
#define MEL_DIALOG_WARN_MULTI_UNSUPPORTED    (1u << 9)
#define MEL_DIALOG_WARN_DEFAULT_PATH_IGNORED (1u << 10)
#define MEL_DIALOG_WARN_PARENT_IGNORED       (1u << 11)

static inline bool mel_dialog_status_ok(Mel_Dialog_Status s) { return (s & MEL_DIALOG_SEVERITY_MASK) == MEL_DIALOG_OK; }
static inline bool mel_dialog_status_warned(Mel_Dialog_Status s) { return (s & MEL_DIALOG_SEVERITY_MASK) == MEL_DIALOG_WARNED; }
static inline bool mel_dialog_status_failed(Mel_Dialog_Status s) { return (s & MEL_DIALOG_SEVERITY_MASK) == MEL_DIALOG_ERROR; }
static inline bool mel_dialog_status_cancelled(Mel_Dialog_Status s) { return (s & MEL_DIALOG_CANCELLED) != 0u; }

typedef struct
{
    const char*        label;
    const char* const* patterns;
    u32                pattern_count;
} Mel_Dialog_Filter;

typedef struct
{
    Mel_Window               parent;
    const char*              title;
    const char*              default_path;
    const Mel_Dialog_Filter* filters;
    u32                      filter_count;
    Mel_Reactor*             reactor;
    Mel_Executor*            deliver;
    const Mel_Alloc*         alloc;
} Mel_Dialog_Open_File_Opt;

typedef struct
{
    Mel_Window               parent;
    const char*              title;
    const char*              default_path;
    const char*              default_name;
    const Mel_Dialog_Filter* filters;
    u32                      filter_count;
    Mel_Reactor*             reactor;
    Mel_Executor*            deliver;
    const Mel_Alloc*         alloc;
} Mel_Dialog_Save_File_Opt;

typedef struct
{
    Mel_Window       parent;
    const char*      title;
    const char*      default_path;
    Mel_Reactor*     reactor;
    Mel_Executor*    deliver;
    const Mel_Alloc* alloc;
} Mel_Dialog_Open_Folder_Opt;

typedef struct
{
    const char* const* paths;
    u32                path_count;
    u32                chosen_filter;
    Mel_Dialog_Status  status;
} Mel_Dialog_Selection;

void mel_dialog_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_dialog_shutdown(void);
bool mel_dialog_available(void);

Mel_Future* mel_dialog_open_file_opt(Mel_Dialog_Open_File_Opt opt);
#define mel_dialog_open_file(...) mel_dialog_open_file_opt((Mel_Dialog_Open_File_Opt){ __VA_ARGS__ })

Mel_Future* mel_dialog_open_files_opt(Mel_Dialog_Open_File_Opt opt);
#define mel_dialog_open_files(...) mel_dialog_open_files_opt((Mel_Dialog_Open_File_Opt){ __VA_ARGS__ })

Mel_Future* mel_dialog_save_file_opt(Mel_Dialog_Save_File_Opt opt);
#define mel_dialog_save_file(...) mel_dialog_save_file_opt((Mel_Dialog_Save_File_Opt){ __VA_ARGS__ })

Mel_Future* mel_dialog_open_folder_opt(Mel_Dialog_Open_Folder_Opt opt);
#define mel_dialog_open_folder(...) mel_dialog_open_folder_opt((Mel_Dialog_Open_Folder_Opt){ __VA_ARGS__ })

const Mel_Dialog_Selection* mel_dialog_future_selection(const Mel_Future* f);
Mel_Dialog_Status           mel_dialog_future_status(const Mel_Future* f);
void                        mel_dialog_future_free(Mel_Future* f);

#ifdef __cplusplus
}
#endif
