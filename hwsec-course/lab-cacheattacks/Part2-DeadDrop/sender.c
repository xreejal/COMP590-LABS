#include "util.h"
#include <sys/mman.h>

#define REGION_BYTES (1 << 21)
#define INPUT_SIZE 128
#define WARMUP_ROUNDS 5
#define CACHE_LINE_SIZE 64
#define SET_SCALE 4
#define ADDRESS_VARIANTS 32

static void *allocate_shared_region(void)
{
    void *region = mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
                        MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB,
                        -1, 0);

    if (region == (void *)-1) {
        perror("mmap() error");
        exit(EXIT_FAILURE);
    }

    return region;
}

static void warm_up_region(void *region, volatile char *sink)
{
    *((char *)region) = 1;

    printf("Warming up...\n");
    for (int round = 0; round < WARMUP_ROUNDS; round++) {
        for (uint64_t offset = 0; offset < REGION_BYTES; offset += CACHE_LINE_SIZE) {
            *sink = *((char *)region + offset);
        }
    }
}

static void keep_sending_value(void *region, int set_index, volatile char *sink)
{
    while (1) {
        for (int way = 0; way < ADDRESS_VARIANTS; way++) {
            uint64_t offset = ((uint64_t)way << 16) | ((uint64_t)set_index << 6);
            *sink = *((char *)region + offset);
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    void *region = allocate_shared_region();
    volatile char sink = 0;
    warm_up_region(region, &sink);

    printf("Sender ready. Please type a message.\n");

    while (1) {
        char line[INPUT_SIZE];
        fgets(line, sizeof(line), stdin);

        int value = atoi(line);
        if (value < 0 || value > 255) {
            printf("Please enter a value between 0 and 255\n");
            continue;
        }

        int set_index = value * SET_SCALE;
        printf("Sending %d (set %d)...\n", value, set_index);
        printf("Press Ctrl+C when receiver shows the correct value.\n");

        keep_sending_value(region, set_index, &sink);
        printf("Done sending %d\n", value);
    }

    munmap(region, REGION_BYTES);
    return 0;
}
