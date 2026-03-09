#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)

int main(int argc, char **argv) {
    void *buf = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

    if (buf == (void*) -1) {
        perror("mmap error");
        exit(EXIT_FAILURE);
    }

    *((char*)buf) = 1;

    printf("Testing L2 cache set-based communication\n");
    printf("This process will FILL a specific cache set\n\n");

    // Warmup
    volatile char tmp;
    for (int w = 0; w < 5; w++) {
        for (uint64_t i = 0; i < BUFF_SIZE; i += 64) {
            tmp = *((char *)buf + i);
        }
    }

    // Test: Can we detectably fill ONE cache set?
    int target_value = 47;
    int target_set = target_value * 4;  // Set 188

    printf("Target value: %d, target set: %d\n", target_value, target_set);
    printf("Press enter to START filling set %d repeatedly...\n", target_set);
    getchar();

    // Strategy: Access MANY addresses that map to the same set
    // Within a 2MB hugepage, we can create addresses with same bits [15:6]
    // by varying bits [20:16] (upper bits of page offset)

    while (1) {
        // Access 32 different addresses that all map to set 'target_set'
        // by varying the upper offset bits
        for (int variant = 0; variant < 32; variant++) {
            uint64_t offset = (variant << 16) | (target_set << 6);
            tmp = *((char *)buf + offset);
        }
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}