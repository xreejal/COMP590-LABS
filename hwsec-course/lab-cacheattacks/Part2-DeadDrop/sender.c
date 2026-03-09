#include "util.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16
#define STRIDE (1<<16)

#define SET_SPACING 32
#define BASE_SET 64

struct node {
    struct node *next;
    char pad[64 - sizeof(struct node*)];
};

void *buf;
struct node *sets[9];

void shuffle(struct node **array, int n) {
    for (int i=n-1;i>0;i--) {
        int j = rand()%(i+1);
        struct node *tmp = array[i];
        array[i] = array[j];
        array[j] = tmp;
    }
}

void build_set(int idx) {
    char *base = (char*)buf;
    struct node *nodes[L2_WAYS];

    int phys = BASE_SET + idx * SET_SPACING;

    for (int i=0;i<L2_WAYS;i++) {
        nodes[i] = (struct node*)(base + phys*64 + i*STRIDE);
    }

    shuffle(nodes, L2_WAYS);

    for (int i=0;i<L2_WAYS-1;i++)
        nodes[i]->next = nodes[i+1];

    nodes[L2_WAYS-1]->next = NULL;

    sets[idx] = nodes[0];
}

void evict_set(int idx) {
    struct node *p = sets[idx];

    while (p)
        p = p->next;

    // second pass for stronger eviction
    p = sets[idx];
    while (p)
        p = p->next;
}

int main() {

    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
        MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB, -1, 0);

    if (buf == (void*)-1) {
        perror("mmap");
        exit(1);
    }

    *((char*)buf) = 1;

    // Build sets for bits + valid
    for (int i=0;i<=8;i++)
        build_set(i);

    printf("Sender ready. Please type a message.\n");

    while (1) {

        char text_buf[128];

        if (!fgets(text_buf,sizeof(text_buf),stdin))
            break;

        int value = atoi(text_buf);

        if (value < 0 || value > 255) {
            printf("Enter value 0-255\n");
            continue;
        }

        printf("Sending %d\n", value);

        long duration = 2000000;

        for (long k=0;k<duration;k++) {

            // VALID signal
            evict_set(8);

            // send bits
            for (int i=0;i<8;i++) {
                if ((value >> i) & 1)
                    evict_set(i);
            }
        }

        printf("Sent.\n");
    }

    return 0;
}