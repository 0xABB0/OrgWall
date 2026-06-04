#include <dialog/backend.h>
#include <window/window.h>
#include <log/log.h>

#include <allocator/allocator.h>

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include <string.h>

bool mel_dialog__plat_available(void) { return true; }

static wchar_t* utf8_to_wide(const Mel_Alloc* a, const char* s)
{
    if (!s)
        return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t* w = (wchar_t*)mel_alloc(a, (usize)n * sizeof(wchar_t));
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char* wide_to_utf8(const Mel_Alloc* a, const wchar_t* w)
{
    if (!w)
        return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0)
        return NULL;
    char* s = (char*)mel_alloc(a, (usize)n);
    if (!s)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static void emit_item(Mel_Dialog_Job* job, IShellItem* item)
{
    PWSTR path = NULL;
    if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path)
    {
        const Mel_Alloc* a = mel_dialog_job_alloc(job);
        char*            u = wide_to_utf8(a, path);
        if (u)
        {
            mel_dialog_job_emit_path(job, u);
            mel_dealloc(a, u);
        }
        CoTaskMemFree(path);
    }
}

typedef struct
{
    wchar_t* label;
    wchar_t* spec;
} Built_Filter;

static wchar_t* join_patterns(const Mel_Alloc* a, Mel_Dialog_Job* job, u32 filter)
{
    u32   pc = mel_dialog_job_filter_pattern_count(job, filter);
    usize cap = 1;
    for (u32 p = 0; p < pc; p++)
    {
        const char* pat = mel_dialog_job_filter_pattern(job, filter, p);
        cap += (pat ? strlen(pat) : 0) + 3;
    }
    wchar_t* buf = (wchar_t*)mel_alloc(a, cap * sizeof(wchar_t));
    if (!buf)
        return NULL;
    buf[0] = 0;
    bool first = true;
    for (u32 p = 0; p < pc; p++)
    {
        const char* pat = mel_dialog_job_filter_pattern(job, filter, p);
        if (!pat)
            continue;
        const char* ext = strrchr(pat, '.');
        wchar_t     one[260];
        if (ext && ext[1] && strcmp(ext, ".*") != 0)
            _snwprintf(one, 260, L"*%hs", ext);
        else if (strchr(pat, '*'))
        {
            wchar_t* w = utf8_to_wide(a, pat);
            wcsncpy(one, w ? w : L"*.*", 259);
            one[259] = 0;
            if (w)
                mel_dealloc(a, w);
        }
        else
            _snwprintf(one, 260, L"*.%hs", pat);
        if (!first)
            wcscat(buf, L";");
        wcscat(buf, one);
        first = false;
    }
    return buf;
}

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    const Mel_Alloc* a = mel_dialog_job_alloc(job);
    u32              request = mel_dialog_job_request(job);
    HRESULT          init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool             owns_com = SUCCEEDED(init);

    IFileDialog* dlg = NULL;
    HRESULT      hr;
    if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
        hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileSaveDialog, (void**)&dlg);
    else
        hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void**)&dlg);

    if (FAILED(hr) || !dlg)
    {
        mel_log_error("dialog", "win32: CoCreateInstance failed (0x%08lx)", (unsigned long)hr);
        if (owns_com)
            CoUninitialize();
        mel_dialog_job_resolve(job, MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE);
        return;
    }

    DWORD opts = 0;
    IFileDialog_GetOptions(dlg, &opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (request & MEL_DIALOG_REQUEST_OPEN_DIR)
        opts |= FOS_PICKFOLDERS;
    if (request & MEL_DIALOG_REQUEST_MULTI)
        opts |= FOS_ALLOWMULTISELECT;
    if (!(request & MEL_DIALOG_REQUEST_SAVE_FILE) && !(request & MEL_DIALOG_REQUEST_OPEN_DIR))
        opts |= FOS_FILEMUSTEXIST;
    if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
        opts |= FOS_OVERWRITEPROMPT;
    IFileDialog_SetOptions(dlg, opts);

    const char* title = mel_dialog_job_title(job);
    if (title)
    {
        wchar_t* w = utf8_to_wide(a, title);
        if (w)
        {
            IFileDialog_SetTitle(dlg, w);
            mel_dealloc(a, w);
        }
    }

    u32           fc = (request & MEL_DIALOG_REQUEST_OPEN_DIR) ? 0 : mel_dialog_job_filter_count(job);
    Built_Filter* built = NULL;
    if (fc > 0)
    {
        built = (Built_Filter*)mel_alloc(a, (usize)fc * sizeof(Built_Filter));
        COMDLG_FILTERSPEC* specs = (COMDLG_FILTERSPEC*)mel_alloc(a, (usize)fc * sizeof(COMDLG_FILTERSPEC));
        if (built && specs)
        {
            for (u32 i = 0; i < fc; i++)
            {
                const char* label = mel_dialog_job_filter_label(job, i);
                built[i].label = utf8_to_wide(a, label ? label : "Files");
                built[i].spec = join_patterns(a, job, i);
                specs[i].pszName = built[i].label ? built[i].label : L"Files";
                specs[i].pszSpec = built[i].spec ? built[i].spec : L"*.*";
            }
            IFileDialog_SetFileTypes(dlg, fc, specs);
        }
        else
        {
            if (built)
            {
                mel_dealloc(a, built);
                built = NULL;
            }
        }
        if (specs)
            mel_dealloc(a, specs);
    }

    const char* dir = mel_dialog_job_default_path(job);
    if (dir)
    {
        wchar_t* w = utf8_to_wide(a, dir);
        if (w)
        {
            IShellItem* si = NULL;
            if (SUCCEEDED(SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void**)&si)) && si)
            {
                IFileDialog_SetFolder(dlg, si);
                IShellItem_Release(si);
            }
            mel_dealloc(a, w);
        }
    }

    const char* name = mel_dialog_job_default_name(job);
    if (name)
    {
        wchar_t* w = utf8_to_wide(a, name);
        if (w)
        {
            IFileDialog_SetFileName(dlg, w);
            mel_dealloc(a, w);
        }
    }

    HWND owner = NULL;
    Mel_Window parent = mel_dialog_job_parent(job);
    if (parent.index != 0)
        owner = (HWND)mel_window_native(parent);

    hr = IFileDialog_Show(dlg, owner);

    Mel_Dialog_Status base = MEL_DIALOG_OK;
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        base = MEL_DIALOG_OK | MEL_DIALOG_CANCELLED;
    }
    else if (FAILED(hr))
    {
        base = MEL_DIALOG_ERROR | MEL_DIALOG_UNAVAILABLE;
    }
    else if (request & MEL_DIALOG_REQUEST_MULTI)
    {
        IShellItemArray* arr = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResults((IFileOpenDialog*)dlg, &arr)) && arr)
        {
            DWORD count = 0;
            IShellItemArray_GetCount(arr, &count);
            for (DWORD i = 0; i < count; i++)
            {
                IShellItem* item = NULL;
                if (SUCCEEDED(IShellItemArray_GetItemAt(arr, i, &item)) && item)
                {
                    emit_item(job, item);
                    IShellItem_Release(item);
                }
            }
            IShellItemArray_Release(arr);
        }
    }
    else
    {
        IShellItem* item = NULL;
        if (SUCCEEDED(IFileDialog_GetResult(dlg, &item)) && item)
        {
            emit_item(job, item);
            IShellItem_Release(item);
        }
        u32 idx = 0;
        if (SUCCEEDED(IFileDialog_GetFileTypeIndex(dlg, &idx)) && idx > 0)
            mel_dialog_job_set_chosen_filter(job, idx - 1);
    }

    if (built)
    {
        for (u32 i = 0; i < fc; i++)
        {
            if (built[i].label)
                mel_dealloc(a, built[i].label);
            if (built[i].spec)
                mel_dealloc(a, built[i].spec);
        }
        mel_dealloc(a, built);
    }

    IFileDialog_Release(dlg);
    if (owns_com)
        CoUninitialize();
    mel_dialog_job_resolve(job, base);
}
