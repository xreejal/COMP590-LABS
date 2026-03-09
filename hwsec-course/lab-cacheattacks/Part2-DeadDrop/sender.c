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

struct node { struct node *next; char pad[64 - sizeof(struct node*)]; };
void *buf;
struct node *sets[9];

// Build set linked list
void build_set(int idx) {
    char *base = (char*)buf;
    struct node *prev = NULL;
    for(int i=0;i<L2_WAYS;i++){
        struct node *n = (struct node*)(base + (BASE_SET + idx*SET_SPACING)*64 + i*STRIDE);
        if(prev) prev->next = n; else sets[idx] = n;
        prev = n;
    }
    prev->next = NULL;
}

// Evict set once
void evict_set(int idx) {
    struct node *p = sets[idx];
    while(p) p = p->next;
}

// Evict a set multiple times (pulse)
void pulse_set(int idx, int count) {
    for(int i=0;i<count;i++) evict_set(idx);
}

int main() {
    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
               MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,-1,0);
    if(buf==(void*)-1){ perror("mmap"); exit(1); }
    *((char*)buf)=1;

    for(int i=0;i<=8;i++) build_set(i);

    printf("Sender ready.\n");

    while(1){
        char line[128];
        if(!fgets(line,sizeof(line),stdin)) break;

        int value = atoi(line);
        if(value<0 || value>255){ printf("0-255 only\n"); continue; }

        printf("Sending %d\n", value);

        // Encode each bit as a number of pulses
        for(int i=0;i<8;i++){
            int bit = (value >> i) & 1;
            int pulses = bit ? 10 + rand()%5 : rand()%3; // high bit = more pulses
            pulse_set(i, pulses);
        }

        // Always send a short pulse on the signal set to mark end
        pulse_set(8, 5);

        printf("Sent.\n");
    }

    return 0;
}