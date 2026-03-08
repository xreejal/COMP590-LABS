#include "util.h"
#include <sys/mman.h>

#define REGION_BYTES (1 << 21)
#define L2_WAYS 16
#define ACCESS_THRESHOLD 125
#define WARMUP_ROUNDS 10
#define WARMUP_STRIDE 64
#define PRIME_DELAY_ITERS 10000000L
#define COOLDOWN_ITERS 500000000L
#define DECODE_ROUNDS 5
#define CONFIRMATION_COUNT 4
#define TOP_SHOW_COUNT 5
#define SENTINEL_VALUE -999

static void *map_region(void)
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

static void warm_up_cache(void *region, volatile char *probe)
{
    *((char *)region) = 1;

    for (int round = 0; round < WARMUP_ROUNDS; round++) {
        for (uint64_t offset = 0; offset < REGION_BYTES; offset += WARMUP_STRIDE) {
            *probe = *((char *)region + offset);
        }
    }
}

static void fill_prime_set(void *region, int target_set, volatile char *probe)
{
    for (int way = 0; way < L2_WAYS; way++) {
        uint64_t offset = ((uint64_t)way << 16) | ((uint64_t)target_set << 6);
        *probe = *((char *)region + offset);
    }
}

static int measure_set_latency(void *region, int target_set)
{
    uint64_t total_time = 0;

    for (int way = 0; way < L2_WAYS; way++) {
        uint64_t offset = ((uint64_t)way << 16) | ((uint64_t)target_set << 6);
        total_time += measure_one_block_access_time((uint64_t)region + offset);
    }

    return (int)(total_time / L2_WAYS);
}

static void delay_cycles(long iterations)
{
    for (volatile long i = 0; i < iterations; i++) {
        ;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    void *region = map_region();
    volatile char probe = 0;

    int order[256];
    int hit_count[256] = {0};
    int hit_latency_sum[256] = {0};
    int rounds_seen = 0;

    printf("Please press enter.\n");
    {
        char ready_buf[2];
        fgets(ready_buf, sizeof(ready_buf), stdin);
    }

    printf("Receiver now listening.\n");
    warm_up_cache(region, &probe);

    while (1) {
        rounds_seen++;

        for (int i = 0; i < 256; i++) {
            order[i] = i;
        }
        for (int i = 255; i > 0; i--) {
            int j = rand() % (i + 1);
            int value = order[i];
            order[i] = order[j];
            order[j] = value;
        }

        for (int idx = 0; idx < 256; idx++) {
            int value = order[idx];
            int set_index = value * 4;

            fill_prime_set(region, set_index, &probe);
            delay_cycles(PRIME_DELAY_ITERS);

            int avg_latency = measure_set_latency(region, set_index);
            if (avg_latency > ACCESS_THRESHOLD) {
                hit_count[value]++;
                hit_latency_sum[value] += avg_latency;
            }
        }

        printf("Round %d complete\n", rounds_seen);

        int best_count = 0;
        int best_value = -1;
        int best_avg_latency = 0;

        for (int v = 0; v < 256; v++) {
            int avg = hit_count[v] > 0 ? (hit_latency_sum[v] / hit_count[v]) : 0;

            if (hit_count[v] > best_count ||
                (hit_count[v] == best_count && avg > best_avg_latency)) {
                best_count = hit_count[v];
                best_value = v;
                best_avg_latency = avg;
            }
        }

        printf("  Top detections: ");
        for (int shown = 0; shown < TOP_SHOW_COUNT; shown++) {
            int top_count = 0;
            int top_value = -1;

            for (int v = 0; v < 256; v++) {
                if (hit_count[v] > top_count) {
                    top_count = hit_count[v];
                    top_value = v;
                }
            }

            if (top_count > 0 && top_value >= 0) {
                printf("val=%d(cnt=%d) ", top_value, top_count);
                hit_count[top_value] = SENTINEL_VALUE;
            }
        }

        for (int v = 0; v < 256; v++) {
            if (hit_count[v] == SENTINEL_VALUE) {
                hit_count[v] = best_count;
            }
        }
        printf("\n");

        if (rounds_seen >= DECODE_ROUNDS && best_count >= CONFIRMATION_COUNT) {
            printf("\n>>> RECEIVED: %d (detected %d/%d times, avg_lat=%d) <<<\n\n",
                   best_value, best_count, rounds_seen, best_avg_latency);
            fflush(stdout);

            for (int v = 0; v < 256; v++) {
                hit_count[v] = 0;
                hit_latency_sum[v] = 0;
            }
            rounds_seen = 0;
            delay_cycles(COOLDOWN_ITERS);
        }
    }

    munmap(region, REGION_BYTES);
    return 0;
}
