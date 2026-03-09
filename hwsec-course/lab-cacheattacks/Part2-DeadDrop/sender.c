#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)

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

        int target_set = value * 4;
        printf("Sending %d (set %d)...\n", value, target_set);
        printf("Press Ctrl+C when receiver shows the correct value.\n");

        // Fill the target set CONTINUOUSLY until user stops
        // This ensures receiver has time to scan all 256 sets multiple times
        while (1) {
            for (int variant = 0; variant < 32; variant++) {
                uint64_t offset = (variant << 16) | (target_set << 6);
                tmp = *((char *)buf + offset);
            }
        }

        printf("Done sending %d\n", value);
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}