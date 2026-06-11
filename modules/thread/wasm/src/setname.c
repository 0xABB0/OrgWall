#include <pthread.h>

int pthread_setname_np(pthread_t thread, const char* name)
{
    (void)thread;
    (void)name;
    return 0;
}
