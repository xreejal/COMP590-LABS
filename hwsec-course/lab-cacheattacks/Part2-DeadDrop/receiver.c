#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

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

#define DATA_SET_BASE 0
#define NUM_BITS 8
#define DATA_THRESHOLD 150
#define MIN_ROUNDS_PER_SLOT 7
#define CONFIDENCE_THRESHOLD 24
#define PRIME_DELAY_ITERS 14000
#define COOLDOWN_ITERS 120000000L
#define ROUND_GAP_CYCLES 10000ULL

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
static uint64_t now_cycles(void)
{
    unsigned int aux = 0;
    unsigned int lo;
    unsigned int hi;

    asm volatile("lfence\n\trdtscp"
                 : "=a"(lo), "=d"(hi), "=c"(aux)
                 : : "memory");
    return ((uint64_t)hi << 32) | lo;
}
#else
static uint64_t now_cycles(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif

static inline void serialize_barrier(void)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    asm volatile("lfence" : : : "memory");
#else
    asm volatile("" : : : "memory");
#endif
}

static void *allocate_channel_region(void)
{
    void *region = mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
                        MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
                        -1, 0);

    if (region == (void *)-1) {
        perror("mmap error");
        exit(EXIT_FAILURE);
    }

    return region;
}

static void warm_up_cache_region(void *region, volatile char *probe)
{
    ((volatile char *)region)[0] = 1;

    for (uint64_t line_cursor = 0; line_cursor < REGION_BYTES; line_cursor += CACHE_LINE_BYTES) {
        *probe = ((volatile char *)region)[line_cursor];
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

static uint64_t measure_set_latency(void *region, int set_index)
{
    uint64_t total = 0;
    volatile char *base = (volatile char *)region;
    uint64_t set_base = (uint64_t)base + (uint64_t)set_index * CACHE_LINE_BYTES;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        total += measure_one_block_access_time((uint64_t)(base + (addr - (uint64_t)base)));
    }

    return total / EV_SET_SIZE;
}

static void delay_busy(long iters)
{
    for (volatile long i = 0; i < iters; i++) {
    }
}

static int read_threshold_override(void)
{
    const char *v = getenv("DEADDROP_THRESHOLD");
    if (!v || *v == '\0') {
        return 0;
    }

    errno = 0;
    char *tail = NULL;
    long parsed = strtol(v, &tail, 10);

    if (tail == v || errno != 0) {
        return 0;
    }

    while (isspace((unsigned char)*tail)) {
        tail++;
    }

    if (*tail != '\0' || parsed < 1 || parsed > 10000) {
        return 0;
    }

    return (int)parsed;
}

static int calibrate_threshold(void *region, volatile char *probe)
{
    uint64_t hit_sum = 0;
    uint64_t miss_sum = 0;
    int sample_steps = 64;

    for (int step = 0; step < sample_steps; step++) {
        int active_bit = rand() % NUM_BITS;
        int tracked_set = DATA_SET_BASE + active_bit;
        uint64_t lat;

        prime_set(region, tracked_set, probe);
        lat = measure_set_latency(region, tracked_set);
        hit_sum += lat;

        for (int other_bit = 0; other_bit < NUM_BITS; other_bit++) {
            if (other_bit == active_bit) {
                continue;
            }
            prime_set(region, DATA_SET_BASE + other_bit, probe);
        }

        miss_sum += measure_set_latency(region, tracked_set);
    }

    if (hit_sum == 0 || miss_sum == 0) {
        return DATA_THRESHOLD;
    }

    uint64_t hit_avg = hit_sum / (uint64_t)sample_steps;
    uint64_t miss_avg = miss_sum / (uint64_t)sample_steps;
    int threshold = (int)((hit_avg + miss_avg) / 2);

    if (threshold <= (int)hit_avg) {
        threshold = (int)hit_avg + 20;
    }
    if (threshold < 1) {
        threshold = DATA_THRESHOLD;
    }

    printf("Calibration: hot=%llu cold=%llu threshold=%d\n",
           (unsigned long long)hit_avg,
           (unsigned long long)miss_avg,
           threshold);

    return threshold;
}

static void print_vote_status(const int *vote_scores)
{
    printf("Top bits: ");
    for (int bit = 0; bit < NUM_BITS; bit++) {
        printf("%d:%d ", bit, vote_scores[bit]);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IOLBF, 0);

    void *region = allocate_channel_region();
    volatile char probe = 0;
    static const int recv_pattern[NUM_BITS] = {0, 5, 2, 7, 1, 6, 3, 4};

    int bit_tally[NUM_BITS] = {0};
    int round_seen = 0;
    int last_value = -1;

    srand((unsigned)time(NULL));

    printf("Receiver ready, waiting for start Enter key.\n");
    {
        char line[2];
        if (!fgets(line, sizeof(line), stdin)) {
            munmap(region, REGION_BYTES);
            return 0;
        }
    }

    warm_up_cache_region(region, &probe);

    int threshold = read_threshold_override();
    if (threshold > 0) {
        printf("Using DEADDROP_THRESHOLD=%d\n", threshold);
    } else {
        threshold = calibrate_threshold(region, &probe);
        printf("Derived threshold=%d\n", threshold);
    }

    printf("Receiver monitoring 8-bit dead-drop slots (sets %d-%d).\n",
           DATA_SET_BASE, DATA_SET_BASE + NUM_BITS - 1);

    while (1) {
        round_seen++;
        for (int i = NUM_BITS - 1; i >= 0; i--) {
            int scan_idx = recv_pattern[i];
            int set_probe = DATA_SET_BASE + scan_idx;

            prime_set(region, set_probe, &probe);
            delay_busy(PRIME_DELAY_ITERS);
            uint64_t latency = measure_set_latency(region, set_probe);

            if ((int)latency > threshold) {
                bit_tally[scan_idx]++;
            } else {
                bit_tally[scan_idx]--;
            }
        }

        printf("Round %d complete\n", round_seen);
        print_vote_status(bit_tally);

        if (round_seen >= MIN_ROUNDS_PER_SLOT) {
            int decoded = 0;
            int confidence = 0;

            for (int bit = 0; bit < NUM_BITS; bit++) {
                if (bit_tally[bit] > 0) {
                    decoded |= 1 << bit;
                }
                confidence += abs(bit_tally[bit]);
            }

            if (decoded != last_value && confidence > CONFIDENCE_THRESHOLD) {
                printf("\n>>> RECEIVED: %d (0x%02x) <<<\n\n", decoded, decoded);
                printf("%d\n", decoded);
                last_value = decoded;
                round_seen = 0;
                for (int bit = 0; bit < NUM_BITS; bit++) {
                    bit_tally[bit] = 0;
                }
                delay_busy(COOLDOWN_ITERS);
            }
        }

        uint64_t gap_target = now_cycles() + ROUND_GAP_CYCLES;
        while (now_cycles() < gap_target) {
            serialize_barrier();
        }
    }

    munmap(region, REGION_BYTES);
    return 0;
}
