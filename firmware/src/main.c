#include <stdio.h>

#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    while (true) {
        puts("VSCP demo W55RP20 startup");
        sleep_ms(1000);
    }
}
