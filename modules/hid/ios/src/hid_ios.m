#include <hid/hid.h>
#include <hid/provider.h>
#include <hid/ios/ios.h>

#include <log/log.h>

#include "../../src/hid_internal.h"

void mel_hid__register_host_providers(const Mel_Alloc* alloc)
{
    (void)alloc;
    mel_log_info("hid", "iOS exposes no public IOKit HID transport; raw HID unavailable (honest absence)");
}

void* mel_hid_ios_device(Mel_Hid_Device d)
{
    (void)d;
    return NULL;
}
