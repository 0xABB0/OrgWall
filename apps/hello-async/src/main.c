#include <stdio.h>

#include "seam.hh"

int main(void) {
    int result = mel_async_probe(40);
    printf("hello-async: sync_wait(just(40) | then(+2)) = %d\n", result);
    return result == 42 ? 0 : 1;
}
