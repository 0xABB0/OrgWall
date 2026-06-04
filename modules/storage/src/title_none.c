#include "storage_internal.h"

#include <storage/backend.h>

const Mel_Storage_Interface* mel_storage_title_interface(void) { return NULL; }

bool mel_storage__title_native_available(void) { return false; }

Mel_Storage* mel_storage__open_title_native(Mel_Storage_Opt opt)
{
    (void)opt;
    return NULL;
}
