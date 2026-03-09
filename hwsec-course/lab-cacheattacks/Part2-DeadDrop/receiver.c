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
#define MAX_MESSAGES 256
#define SAMPLES_PER_ROUND L2_NUM_SETS
#define PRIME_DELAY_ITERS 12000
#define COOLDOWN_ITERS 200000000L
#define DECODE_ROUNDS 4
#define CONFIRMATION_COUNT 4
#define TOP_SHOW_COUNT 5
#define DEFAULT_THRESHOLD 150

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

    for (uint64_t offset = 0; offset < REGION_BYTES; offset += CACHE_LINE_BYTES) {
        *probe = ((volatile char *)region)[offset];
    }
}

static void prime_set(void *region, int set_index, volatile char *probe)
{
    volatile char *base = (volatile char *)region;
    uint64_t base_addr = (uint64_t)base + (uint64_t)set_index * CACHE_LINE_BYTES;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t offset = base_addr + (uint64_t)way * SET_STRIDE - (uint64_t)base;
        *probe = base[offset];
    }
}

static uint64_t measure_set_latency(void *region, int set_index)
{
    uint64_t total = 0;
    volatile char *base = (volatile char *)region;
    uint64_t base_addr = (uint64_t)base + (uint64_t)set_index * CACHE_LINE_BYTES;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t offset = base_addr + (uint64_t)way * SET_STRIDE - (uint64_t)base;
        total += measure_one_block_access_time((uint64_t)(base + offset));
    }

    return total / EV_SET_SIZE;
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
    int rounds = 64;

    for (int i = 0; i < rounds; i++) {
        int set = rand() % MAX_MESSAGES;

        prime_set(region, set, probe);
        hit_sum += measure_set_latency(region, set);

        for (int other = 0; other < MAX_MESSAGES; other++) {
            if (other == set) {
                continue;
            }
            prime_set(region, other, probe);
        }
        miss_sum += measure_set_latency(region, set);
    }

    if (hit_sum == 0 || miss_sum == 0) {
        return DEFAULT_THRESHOLD;
    }

    uint64_t hit_avg = hit_sum / (uint64_t)rounds;
    uint64_t miss_avg = miss_sum / (uint64_t)rounds;
    int threshold = (int)((hit_avg + miss_avg) / 2);

    if (threshold <= (int)hit_avg) {
        threshold = (int)hit_avg + 20;
    }
    if (threshold < 1) {
        threshold = DEFAULT_THRESHOLD;
    }

    printf("Calibration: hit=%llu miss=%llu threshold=%d\n",
           (unsigned long long)hit_avg,
           (unsigned long long)miss_avg,
           threshold);

    return threshold;
}

static void print_top_hits(const int *hit_count)
{
    int ranked[MAX_MESSAGES];

    for (int i = 0; i < MAX_MESSAGES; i++) {
        ranked[i] = hit_count[i];
    }

    printf("Top detections: ");
    for (int shown = 0; shown < TOP_SHOW_COUNT; shown++) {
        int best_value = -1;
        int best_count = 0;

        for (int v = 0; v < MAX_MESSAGES; v++) {
            if (ranked[v] > best_count) {
                best_count = ranked[v];
                best_value = v;
            }
        }

        if (best_value < 0 || best_count <= 0) {
            break;
        }

        printf("%d(cnt=%d) ", best_value, best_count);
        ranked[best_value] = -1;
    }
    printf("\n");
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

    void *region = allocate_channel_region();
    volatile char probe = 0;

    int vote[MAX_MESSAGES] = {0};
    int latency_sum[MAX_MESSAGES] = {0};
    int rounds_seen = 0;

    int order[L2_NUM_SETS];
    uint64_t latencies[MAX_MESSAGES];
    int use_threshold = 0;

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
        use_threshold = 1;
    } else {
        use_threshold = 0;
        int calib_threshold = calibrate_threshold(region, &probe);
        if (calib_threshold > 0) {
            threshold = calib_threshold;
        }
    }

    printf("Receiver monitoring values 0-255 on L2 set indices.\n");

    while (1) {
        rounds_seen++;

        for (int i = 0; i < L2_NUM_SETS; i++) {
            order[i] = i;
        }
        shuffle_order(order, L2_NUM_SETS);

        for (int idx = 0; idx < L2_NUM_SETS; idx++) {
            int set_index = order[idx];
            prime_set(region, set_index, &probe);
        }

        delay_busy(PRIME_DELAY_ITERS);

        for (int set_index = MAX_MESSAGES - 1; set_index >= 0; set_index--) {
            uint64_t lat = measure_set_latency(region, set_index);
            latencies[set_index] = lat;
            if (use_threshold && (int)lat > threshold) {
                vote[set_index]++;
                latency_sum[set_index] += (int)lat;
            }
        }

        if (!use_threshold) {
            int best_set = 0;
            uint64_t best_lat = latencies[0];
            for (int i = 1; i < MAX_MESSAGES; i++) {
                if (latencies[i] > best_lat) {
                    best_lat = latencies[i];
                    best_set = i;
                }
            }
            vote[best_set]++;
            latency_sum[best_set] += (int)best_lat;
        }

        printf("Round %d complete\n", rounds_seen);

        int best_cnt = 0;
        int best_val = -1;
        int best_avg = 0;
        for (int v = 0; v < MAX_MESSAGES; v++) {
            int avg = vote[v] > 0 ? (latency_sum[v] / vote[v]) : 0;
            if (vote[v] > best_cnt || (vote[v] == best_cnt && avg > best_avg)) {
                best_cnt = vote[v];
                best_val = v;
                best_avg = avg;
            }
        }

        print_top_hits(vote);

        if (rounds_seen >= DECODE_ROUNDS && best_cnt >= CONFIRMATION_COUNT) {
            printf("\n>>> RECEIVED: %d (hits=%d/%d, avg_lat=%d) <<<\n\n",
                   best_val, best_cnt, rounds_seen, best_avg);
            fflush(stdout);

            for (int i = 0; i < MAX_MESSAGES; i++) {
                vote[i] = 0;
                latency_sum[i] = 0;
            }
            rounds_seen = 0;
            delay_busy(COOLDOWN_ITERS);
        }
    }

    munmap(region, REGION_BYTES);
    return 0;
}
