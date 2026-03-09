#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)
#define L2_SIZE (1024 * 1024)
#define THRESHOLD 100

int main(int argc, char **argv)
{
    void *buf = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

    if (buf == (void*) -1) {
        perror("mmap error");
        exit(EXIT_FAILURE);
    }

    *((char*)buf) = 1;

    printf("Please press enter.\n");
    char text_buf[128];
    fgets(text_buf, sizeof(text_buf), stdin);

    // Warmup
    printf("Warming up...\n");
    volatile char tmp;
    for (int w = 0; w < 10; w++) {
        for (uint64_t i = 0; i < BUFF_SIZE; i += 64) {
            tmp = *((char *)buf + i);
        }
    }

    printf("Receiver now listening.\n");

    int eviction_count = 0;
    int idle_rounds = 0;

    while (1) {
        // PRIME: Fill our L2 cache
        for (uint64_t offset = 0; offset < L2_SIZE; offset += 64) {
            tmp = *((char *)buf + offset);
        }

        // Small wait
        for (volatile long i = 0; i < 50000000; i++);

        // PROBE: Measure average access time
        uint64_t start = __builtin_ia32_rdtsc();
        for (uint64_t offset = 0; offset < L2_SIZE; offset += 64) {
            tmp = *((char *)buf + offset);
        }
        uint64_t end = __builtin_ia32_rdtsc();

        uint64_t total_cycles = end - start;
        uint64_t avg_cycles = total_cycles / (L2_SIZE / 64);

        if (avg_cycles > THRESHOLD) {
            // Eviction detected
            eviction_count++;
            idle_rounds = 0;
        } else {
            // No eviction
            idle_rounds++;

            // If we've been idle for a while and have evictions counted, print result
            if (idle_rounds >= 3 && eviction_count > 0) {
                printf("%d\n", eviction_count - 1);
                fflush(stdout);
                eviction_count = 0;
                idle_rounds = 0;

                // Cooldown
                for (volatile long i = 0; i < 300000000; i++);
            }
        }
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}