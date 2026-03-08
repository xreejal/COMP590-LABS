#include "util.h"

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
    (void)0;
}
#endif

CYCLES measure_one_block_access_time(ADDR_PTR addr)
{
    serialise_cpu();
    uint64_t start = read_tsc();

    uint8_t value = *(volatile uint8_t *)addr;

    serialise_cpu();
    uint64_t end = read_tsc();

    (void)value;
    return (CYCLES)(end - start);
}

void clflush(ADDR_PTR addr)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    asm volatile("clflush (%0)\n\t" : : "r"(addr) : "memory");
    asm volatile("mfence\n\t" : : : "memory");
#else
    (void)addr;
#endif
}
