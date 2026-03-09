#ifndef PART1_UTILITY_H_
#define PART1_UTILITY_H_

#include <stdint.h>
#include <stddef.h>

#define L1_SIZE_BYTES (32 * 1024)
#define L2_SIZE_BYTES (256 * 1024)
#define L3_SIZE_BYTES (8 * 1024 * 1024)
#define CACHE_LINE_BYTES 64
#define SAMPLES 10000

uint32_t measure_one_block_access_time(uint64_t addr);
void clflush(void *addr);
uint64_t rdtscp(void);
uint64_t rdtscp64(void);
uint64_t rdtscp_serialized(void);
void print_results_json(const uint64_t *l1, const uint64_t *l2, const uint64_t *l3,
                       const uint64_t *dram, size_t sample_count);
void print_results(const uint64_t *l1, const uint64_t *l2, const uint64_t *l3,
                  const uint64_t *dram, size_t sample_count);
void print_results_for_python(const uint64_t *l1, const uint64_t *l2, const uint64_t *l3,
                             const uint64_t *dram, size_t sample_count);

#endif
