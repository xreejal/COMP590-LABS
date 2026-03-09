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
uint64_t thresholds[9];

static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

void build_set(int idx) {
    char *base = (char*)buf;
    struct node *prev = NULL;

    for(int i=0;i<L2_WAYS;i++){
        struct node *n = (struct node*)(base + (BASE_SET + idx*SET_SPACING)*64 + i*STRIDE);
        if(prev) prev->next = n;
        else sets[idx] = n;
        prev = n;
    }
    prev->next = NULL;
}

uint64_t traverse_set(int idx) {
    struct node *p = sets[idx];
    uint64_t t0 = rdtscp();
    while(p) p = p->next;
    return rdtscp() - t0;
}

void calibrate() {
    for(int i=0;i<=8;i++){
        uint64_t sum=0;

        for(int j=0;j<500;j++){
            traverse_set(i);
            sum += traverse_set(i);
        }

        thresholds[i] = (sum/500) * 2;
    }
}

int signal_high() {
    int consecutive = 0;
    while(1) {
        if(traverse_set(8) > thresholds[8]) consecutive++;
        else consecutive = 0;
        if(consecutive >= 15) return 1; // 15 consecutive readings above threshold
        for(volatile int w=0; w<100; w++);
    }
}

void wait_for_start() {
    int consecutive = 0;

    while(1){
        if(signal_high()) consecutive++;
        else consecutive = 0;

        if(consecutive >= 20) return;

        for(volatile int w=0; w<2000; w++);
    }
}

void wait_for_drop() {
    int lows = 0;

    while(lows < 10){
        if(!signal_high()) lows++;
        else lows = 0;

        for(volatile int w=0; w<3000; w++);
    }
}

int main() {

    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
        MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,-1,0);

    if(buf==(void*)-1){ perror("mmap"); exit(1); }

    *((char*)buf)=1;

    for(int i=0;i<=8;i++) build_set(i);

    calibrate();

    printf("Press Enter to start.\n"); getchar();
    printf("Listening...\n");

    while(1){

        wait_for_start();

        int bits[8]={0};
        int samples = 0;

        const int MAX_SAMPLES = 120;

        for(int s=0; s<MAX_SAMPLES; s++){

            if(!signal_high()) continue;

            samples++;

            for(int b=0;b<8;b++)
                if(traverse_set(b) > thresholds[b])
                    bits[b]++;

            for(volatile int w=0; w<2000; w++);
        }

        if(samples < 20){
            wait_for_drop();
            continue;
        }

        int value=0;

        for(int b=0;b<8;b++)
            if(bits[b] > samples/2)
                value |= 1<<b;

        printf("%d\n",value);

        wait_for_drop();
    }
}