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
uint64_t thresholds[9];

// RDTSCP timer
static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

// Build a linked list for a set
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

// Prime a set (traverse it)
void prime_set(int idx) {
    struct node *p = sets[idx];
    while(p) p = p->next;
}

// Probe a set and return elapsed cycles
uint64_t probe_set(int idx) {
    uint64_t start = rdtscp();
    struct node *p = sets[idx];
    while(p) p = p->next;
    return rdtscp() - start;
}

// Calibrate thresholds
void calibrate() {
    printf("Calibrating...\n");
    for(int i=0;i<=8;i++){
        uint64_t sum=0;
        for(int j=0;j<500;j++){
            prime_set(i);
            sum += probe_set(i);
        }
        thresholds[i] = (sum/500) * 2; // safer factor
        printf("Set %d threshold = %lu\n", i, thresholds[i]);
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

    printf("Please press enter.\n");
    getchar();
    printf("Receiver listening...\n");

    while(1){
        // Detect signal on set 8
        prime_set(8);
        for(volatile int w=0; w<2500; w++);  // slightly longer wait
        if(probe_set(8) > thresholds[8]){
            int bits[8] = {0};
            int valid_samples = 0;
            const int samples = 100;

            for(int s=0;s<samples;s++){
                // Prime all
                for(int i=0;i<=8;i++) prime_set(i);
                for(volatile int w=0; w<6000; w++); // longer delay for stabilization

                // Probe
                if(probe_set(8) > thresholds[8]){
                    valid_samples++;
                    for(int i=0;i<8;i++){
                        if(probe_set(i) > thresholds[i]) bits[i]++;
                    }
                }
            }

            if(valid_samples > samples/2){
                int value=0;
                for(int i=0;i<8;i++){
                    if(bits[i] > valid_samples*0.7) value |= (1<<i); // lower threshold to 70%
                }
                printf("%d\n", value);
            }

            // Wait until signal consistently drops
            int lows=0;
            while(lows<12){ // more consecutive lows required
                prime_set(8);
                for(volatile int w=0; w<6000; w++);
                if(probe_set(8) < thresholds[8]) lows++;
                else lows=0;
            }
        }
    }

    return 0;
}