#include "util.h"
#include <sys/mman.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0
#endif

#define REGION_BYTES (2ULL * 1024ULL * 1024ULL)
#define CACHE_LINE_BYTES 64ULL
#define MAX_MESSAGES 256
#define L2_NUM_SETS 1024
#define L2_ASSOCIATIVITY 4
#define EV_SET_SIZE (L2_ASSOCIATIVITY + 2)
#define SET_STRIDE (CACHE_LINE_BYTES * L2_NUM_SETS)
#define INPUT_SIZE 128

static void *allocate_channel_region(void)
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

static void warm_up_channel(void *region, volatile char *sink)
{
    ((volatile char *)region)[0] = 1;

    for (unsigned long i = 0; i < REGION_BYTES; i += CACHE_LINE_BYTES) {
        *sink = ((volatile char *)region)[i];
    }
}

static int parse_input_value(const char *line, int *out)
{
    while (line && isspace((unsigned char)*line)) {
        line++;
    }

    if (!line || *line == '\0') {
        return 0;
    }

    errno = 0;
    char *tail = NULL;
    long parsed = strtol(line, &tail, 10);

    if (tail == line || errno != 0) {
        return 0;
    }

    while (isspace((unsigned char)*tail)) {
        tail++;
    }

    if (*tail != '\0' || parsed < 0 || parsed > 255) {
        return 0;
    }

    *out = (int)parsed;
    return 1;
}

static void hammer_set(void *region, int set_index, volatile char *sink)
{
    const uint64_t base = (uint64_t)region;

    while (1) {
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0) {
                for (int way = 0; way < EV_SET_SIZE; way++) {
                    uint64_t offset = (uint64_t)set_index * CACHE_LINE_BYTES
                                      + (uint64_t)way * SET_STRIDE;
                    *sink = ((volatile char *)(base + offset))[0];
                }
            } else {
                for (int way = EV_SET_SIZE - 1; way >= 0; way--) {
                    uint64_t offset = (uint64_t)set_index * CACHE_LINE_BYTES
                                      + (uint64_t)way * SET_STRIDE;
                    *sink = ((volatile char *)(base + offset))[0];
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IOLBF, 0);

    void *region = allocate_channel_region();
    volatile char sink = 0;

    warm_up_channel(region, &sink);

    printf("DeadDrop sender ready.\n");
    printf("Type an integer [0,255]. Ctrl-D to stop.\n");

    while (1) {
        char line[INPUT_SIZE];

        if (!fgets(line, sizeof(line), stdin)) {
            printf("No input. Exiting.\n");
            break;
        }

        int value = 0;
        if (!parse_input_value(line, &value)) {
            printf("Invalid input. Enter one integer between 0 and 255.\n");
            continue;
        }

        int set_index = value;
        printf("Sending value=%d mapped to set=%d.\n", value, set_index);
        printf("Press Ctrl+C when receiver confirms.\n");

        hammer_set(region, set_index, &sink);
        printf("Done sending %d.\n", value);
    }

    munmap(region, REGION_BYTES);
    return 0;
}
