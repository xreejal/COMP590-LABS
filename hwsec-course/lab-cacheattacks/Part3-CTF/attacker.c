#include "util.h"
#include <sys/mman.h>

#define BUFF_SIZE (2*1024*1024)
#define CACHE_LINE 64
#define NUM_SETS 1024

int main(int argc, char const *argv[]) {

    int flag = -1;

    char *buf = mmap(NULL, BUFF_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS |
                     MAP_PRIVATE | MAP_HUGETLB,
                     -1, 0);

    if (buf == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    buf[0] = 1;

    char *set_addr[NUM_SETS];

    for (int i = 0; i < NUM_SETS; i++) {
    set_addr[i] = buf + i * 65536;
}

while (1) {

    // PRIME
    for (int i = 0; i < NUM_SETS; i++)
        *(volatile char*)set_addr[i];

    for (volatile int i = 0; i < 100000; i++);

    // PROBE
    for (int i = 0; i < NUM_SETS; i++)
        times[i] = measure_one_block_access_time(set_addr[i]);

    uint64_t max_time = 0;

    for (int i = 0; i < NUM_SETS; i++) {
        if (times[i] > max_time) {
            max_time = times[i];
            flag = i;
        }
    }

    break;
}

    printf("Flag: %d\n", flag);

    return 0;
}