#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16

int main(int argc, char **argv) {
    void *buf = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

    if (buf == (void*) -1) {
        perror("mmap error");
        exit(EXIT_FAILURE);
    }

    *((char*)buf) = 1;

    printf("Testing L2 cache eviction DETECTION\n\n");

    // Warmup
    volatile char tmp;
    for (int w = 0; w < 10; w++) {
        for (uint64_t i = 0; i < BUFF_SIZE; i += 64) {
            tmp = *((char *)buf + i);
        }
    }

    int target_value = 47;
    int target_set = target_value * 4;

    printf("Will monitor set %d (value %d)\n", target_set, target_value);
    printf("Start test_eviction in another terminal, then press enter here\n");
    getchar();

    printf("\nMonitoring... (Ctrl+C to stop)\n\n");

    while (1) {
        // PRIME: Fill our addresses for this set
        for (int way = 0; way < L2_WAYS; way++) {
            uint64_t offset = (way << 16) | (target_set << 6);
            tmp = *((char *)buf + offset);
        }

        // Small delay
        for (volatile long i = 0; i < 50000000; i++);

        // PROBE: Measure latency
        uint64_t total_time = 0;
        for (int way = 0; way < L2_WAYS; way++) {
            uint64_t offset = (way << 16) | (target_set << 6);
            uint64_t time = measure_one_block_access_time((uint64_t)buf + offset);
            total_time += time;
        }

        int avg = total_time / L2_WAYS;
        printf("Set %d avg latency: %d cycles %s\n", target_set, avg,
               avg > 80 ? " <-- EVICTED!" : "");

        for (volatile long i = 0; i < 100000000; i++);
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}