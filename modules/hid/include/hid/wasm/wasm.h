#pragma once

#include <hid/hid.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Opaque WebHID device id (an index into the JS-side device table) behind the handle, or -1 when
// closed, absent, or when the browser lacks the WebHID API. There is no native pointer to surface;
// the JS HIDDevice object lives behind the Emscripten boundary.
int mel_hid_wasm_device_id(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
