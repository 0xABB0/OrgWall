#include "storage_internal.h"

#include <storage/backend.h>

#include <fs/paths.h>

#include <allocator/heap.h>
#include <log/log.h>

Mel_Storage* mel_storage__open_title_native(Mel_Storage_Opt opt);

Mel_Storage* mel_storage_open_title_opt(Mel_Storage_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    if (mel_storage__title_native_available())
    {
        opt.alloc = alloc;
        Mel_Storage* st = mel_storage__open_title_native(opt);
        if (st)
            return st;
        mel_log_warn("storage", "open_title: native title backend failed to initialise; falling back to bundled-folder storage");
    }

    return mel_storage__open_fs_folder(MEL_FS_FOLDER_BASE, opt.reactor, alloc, false, false);
}
