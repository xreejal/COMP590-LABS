#include "utility.h"

#include <stdio.h>
#include <time.h>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
static inline uint64_t read_tsc(void)
{
    unsigned int aux = 0;
    unsigned int lo;
    unsigned int hi;

    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void serialise_cpu(void)
{
    asm volatile("lfence\n\t" : : : "memory");
}
#else
static inline uint64_t read_tsc(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void serialise_cpu(void)
{
    /* No-op on non-x86 hosts. */
    (void)0;
}
#endif

uint64_t rdtscp(void)
{
    serialise_cpu();
    return read_tsc();
}

uint64_t rdtscp64(void)
{
    return rdtscp();
}

uint64_t rdtscp_serialized(void)
{
    return rdtscp();
}

uint32_t measure_one_block_access_time(uint64_t addr)
{
    serialise_cpu();
    uint64_t start = read_tsc();

    uint8_t value = *(volatile uint8_t *)addr;

    serialise_cpu();
    uint64_t end = read_tsc();

    (void)value;
    return (uint32_t)(end - start);
}

void clflush(void *addr)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    asm volatile("clflush (%0)\n\t" : : "r"(addr) : "memory");
    asm volatile("mfence\n\t" : : : "memory");
#else
    (void)addr;
#endif
}

void print_results_json(const uint64_t *l1, const uint64_t *l2, const uint64_t *l3,
                       const uint64_t *dram, size_t sample_count)
{
    printf("{\"l1\":[");
    for (size_t i = 0; i < sample_count; i++) {
        if (i) {
            printf(",");
        }
        printf("%llu", (unsigned long long)l1[i]);
    }

    printf("],\"l2\":[");
    for (size_t i = 0; i < sample_count; i++) {
        if (i) {
            printf(",");
        }
        printf("%llu", (unsigned long long)l2[i]);
    }

    printf("],\"l3\":[");
    for (size_t i = 0; i < sample_count; i++) {
        if (i) {
            printf(",");
        }
        printf("%llu", (unsigned long long)l3[i]);
    }

    printf("],\"dram\":[");
    for (size_t i = 0; i < sample_count; i++) {
        if (i) {
            printf(",");
        }
        printf("%llu", (unsigned long long)dram[i]);
    }

    printf("]}\n");
}

void print_results(const uint64_t *l1, const uint64_t *l2, const uint64_t *l3,
                  const uint64_t *dram, size_t sample_count)
{
    print_results_json(l1, l2, l3, dram, sample_count);
}

void print_results_for_python(const uint64_t *l1, const uint64_t *l2, const uint64_t *l3,
                             const uint64_t *dram, size_t sample_count)
{
    print_results_json(l1, l2, l3, dram, sample_count);
}
