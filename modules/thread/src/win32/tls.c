#include <thread/tls.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static_assert(sizeof(DWORD) <= MEL_TLS_STORAGE_SIZE, "MEL_TLS_STORAGE_SIZE too small");

#define MEL__TLS(k) (*(DWORD*)(k)->_storage)

bool mel_tls_init(Mel_Tls* k, Mel_Tls_Dtor dtor)
{
    (void)dtor;
    DWORD idx = TlsAlloc();
    if (idx == TLS_OUT_OF_INDEXES) return false;
    MEL__TLS(k) = idx;
    return true;
}

void mel_tls_destroy(Mel_Tls* k)
{
    TlsFree(MEL__TLS(k));
}

void* mel_tls_get(Mel_Tls* k)
{
    return TlsGetValue(MEL__TLS(k));
}

void mel_tls_set(Mel_Tls* k, void* value)
{
    TlsSetValue(MEL__TLS(k), value);
}
