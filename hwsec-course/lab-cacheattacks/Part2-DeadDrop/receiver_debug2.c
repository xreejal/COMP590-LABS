#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)
#define L2_SETS 1024
#define L2_WAYS 16
#define CACHE_LINE_SIZE 64

int main(int argc, char **argv)
{
    void *buf = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

    if (buf == (void*) -1) {
        perror("mmap error");
        exit(EXIT_FAILURE);
    }

    *((char*)buf) = 1;

    // WARMUP
    printf("Warming up...\n");
    volatile char tmp_warmup;
    for (int warmup = 0; warmup < 10; warmup++) {
        for (int val = 0; val < 256; val++) {
            int target_set = val * 4;
            for (int way = 0; way < L2_WAYS; way++) {
                uint64_t offset = (way << 16) | (target_set << 6);
                tmp_warmup = *((char *)buf + offset);
            }
        }
    }

    printf("Debug receiver - Type a value you expect to receive (0-255): ");
    char text_buf[128];
    fgets(text_buf, sizeof(text_buf), stdin);
    int expected = atoi(text_buf);
    int expected_set = expected * 4;

    printf("Monitoring for value %d (set %d)\n", expected, expected_set);
    printf("Now start the sender and send %d\n\n", expected);

    for (int round = 0; round < 10; round++) {
        printf("=== Round %d ===\n", round);

        // PRIME
        volatile char tmp;
        for (int val = 0; val < 256; val++) {
            int target_set = val * 4;
            for (int way = 0; way < L2_WAYS; way++) {
                uint64_t offset = (way << 16) | (target_set << 6);
                tmp = *((char *)buf + offset);
            }
        }

        // Wait
        for (volatile long i = 0; i < 150000000; i++);

        // PROBE specific value and show comparison
        int latencies[256];
        for (int val = 0; val < 256; val++) {
            int target_set = val * 4;
            uint64_t total_time = 0;
            for (int way = 0; way < L2_WAYS; way++) {
                uint64_t offset = (way << 16) | (target_set << 6);
                uint64_t time = measure_one_block_access_time((uint64_t)buf + offset);
                total_time += time;
            }
            latencies[val] = total_time / L2_WAYS;
        }

        // Find max
        int max_val = 0;
        int max_lat = 0;
        for (int val = 0; val < 256; val++) {
            if (latencies[val] > max_lat) {
                max_lat = latencies[val];
                max_val = val;
            }
        }

        printf("Expected value %d (set %d): %d cycles\n", expected, expected_set, latencies[expected]);
        printf("Max detected value %d (set %d): %d cycles\n", max_val, max_val * 4, max_lat);
        printf("Difference: %d cycles\n\n", max_lat - latencies[expected]);

        // Wait before next round
        for (volatile long i = 0; i < 100000000; i++);
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}