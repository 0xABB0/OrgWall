#include <thread/tls.h>

#include <pthread.h>

static_assert(sizeof(pthread_key_t) <= MEL_TLS_STORAGE_SIZE, "MEL_TLS_STORAGE_SIZE too small");
static_assert(_Alignof(pthread_key_t) <= MEL_TLS_STORAGE_ALIGN, "MEL_TLS_STORAGE_ALIGN too small");

#define MEL__TLS(k) (*(pthread_key_t*)(k)->_storage)

bool mel_tls_init(Mel_Tls* k, Mel_Tls_Dtor dtor)
{
    return pthread_key_create(&MEL__TLS(k), dtor) == 0;
}

void mel_tls_destroy(Mel_Tls* k)
{
    pthread_key_delete(MEL__TLS(k));
}

void* mel_tls_get(Mel_Tls* k)
{
    return pthread_getspecific(MEL__TLS(k));
}

void mel_tls_set(Mel_Tls* k, void* value)
{
    pthread_setspecific(MEL__TLS(k), value);
}
