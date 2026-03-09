#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)
#define L2_SIZE (1024 * 1024)

int main(int argc, char **argv)
{
    void *buf = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

    if (buf == (void*) -1) {
        perror("mmap() error");
        exit(EXIT_FAILURE);
    }

    *((char *)buf) = 1;

    // Warmup
    printf("Warming up...\n");
    volatile char tmp;
    for (int w = 0; w < 5; w++) {
        for (uint64_t i = 0; i < BUFF_SIZE; i += 64) {
            tmp = *((char *)buf + i);
        }
    }

    printf("Sender ready. Please type a message.\n");

    while (1) {
        char text_buf[128];
        fgets(text_buf, sizeof(text_buf), stdin);

        int value = atoi(text_buf);
        if (value < 0 || value > 255) {
            printf("Please enter a value between 0 and 255\n");
            continue;
        }

        printf("Sending %d...\n", value);

        // Strategy: Encode using NUMBER OF EVICTIONS
        // Send 'value' evictions of the entire L2 cache
        // Receiver will count how many evictions it detects

        for (int transmit = 0; transmit < value + 1; transmit++) {
            // Fill entire L2 cache with our buffer
            for (int repeat = 0; repeat < 3; repeat++) {
                for (uint64_t offset = 0; offset < L2_SIZE; offset += 64) {
                    tmp = *((char *)buf + offset);
                }
            }

            // Pause between transmissions so receiver can detect
            for (volatile long i = 0; i < 200000000; i++);
        }

        printf("Done sending %d\n", value);
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}