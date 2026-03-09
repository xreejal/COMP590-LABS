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

    printf("Debug receiver - showing latency measurements\n");
    printf("Please press enter.\n");
    char text_buf[128];
    fgets(text_buf, sizeof(text_buf), stdin);

    printf("Receiver now listening...\n");

    int round = 0;
    while (round < 5) {  // Only run 5 rounds for debug
        round++;
        printf("\n=== Round %d ===\n", round);

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

        // PROBE and show top 10 slowest
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

        // Find top 10
        printf("Top 10 slowest cache sets:\n");
        for (int i = 0; i < 10; i++) {
            int max_val = 0;
            int max_lat = 0;
            for (int val = 0; val < 256; val++) {
                if (latencies[val] > max_lat) {
                    max_lat = latencies[val];
                    max_val = val;
                }
            }
            printf("  Value %3d: %d cycles\n", max_val, max_lat);
            latencies[max_val] = 0;  // Mark as used
        }

        // Wait before next round
        for (volatile long i = 0; i < 100000000; i++);
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}