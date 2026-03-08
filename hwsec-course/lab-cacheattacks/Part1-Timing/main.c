#include "utility.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void touch_memory(volatile uint8_t *buffer, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += CACHE_LINE_BYTES) {
        buffer[i]++;
    }
}

static void ensure_allocation(volatile uint8_t *buf, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += CACHE_LINE_BYTES) {
        buf[i] = (uint8_t)(i & 0xFF);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IOLBF, 0);

    uint64_t l1_latency[SAMPLES] = {0};
    uint64_t l2_latency[SAMPLES] = {0};
    uint64_t l3_latency[SAMPLES] = {0};
    uint64_t dram_latency[SAMPLES] = {0};

    const size_t target_size = CACHE_LINE_BYTES;
    const size_t l1_evict_size = (L1_SIZE_BYTES * 3) / 2;
    const size_t l2_evict_size = (L2_SIZE_BYTES * 3) / 2;
    const size_t l3_evict_size = (L3_SIZE_BYTES * 3) / 2;
    const size_t large_buffer_size = 8 * 1024 * 1024;

    volatile uint8_t *target = malloc(target_size);
    volatile uint8_t *l1_evict = malloc(l1_evict_size);
    volatile uint8_t *buf8mb = malloc(large_buffer_size);
    volatile uint8_t *l2_evict = malloc(l2_evict_size);
    volatile uint8_t *l3_evict = malloc(l3_evict_size);

    if (!target || !l1_evict || !buf8mb || !l2_evict || !l3_evict) {
        fprintf(stderr, "allocation failure\n");
        return 1;
    }

    ensure_allocation((uint8_t *)target, target_size);
    ensure_allocation((uint8_t *)l1_evict, l1_evict_size);
    ensure_allocation((uint8_t *)buf8mb, large_buffer_size);
    ensure_allocation((uint8_t *)l2_evict, l2_evict_size);
    ensure_allocation((uint8_t *)l3_evict, l3_evict_size);

    for (int i = 0; i < SAMPLES; i++) {
        target[0] = 0x41;
        clflush((void *)target);
        l1_latency[i] = measure_one_block_access_time((uint64_t)target);
    }

    for (int i = 0; i < SAMPLES; i++) {
        clflush((void *)target);
        dram_latency[i] = measure_one_block_access_time((uint64_t)target);
    }

    for (int i = 0; i < SAMPLES; i++) {
        target[0] = 0x41;
        touch_memory((uint8_t *)l1_evict, l1_evict_size);
        l2_latency[i] = measure_one_block_access_time((uint64_t)target);
    }

    for (int i = 0; i < SAMPLES; i++) {
        target[0] = 0x41;
        touch_memory((uint8_t *)l3_evict, l3_evict_size);
        l3_latency[i] = measure_one_block_access_time((uint64_t)target);
    }

    print_results_for_python(l1_latency, l2_latency, l3_latency, dram_latency, SAMPLES);

    free((void *)l1_evict);
    free((void *)target);
    free((void *)buf8mb);
    free((void *)l2_evict);
    free((void *)l3_evict);

    return 0;
}
