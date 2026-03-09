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

// RDTSCP timing
static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

// Build linked list for a set
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

// Traverse a set and return access time
uint64_t traverse_set(int idx) {
    struct node *p = sets[idx];
    uint64_t t0 = rdtscp();
    while(p) p = p->next;
    return rdtscp() - t0;
}

// Calibrate thresholds (~1.3x of average)
void calibrate() {
    for(int i=0;i<=8;i++){
        uint64_t sum=0;
        for(int j=0;j<500;j++){
            traverse_set(i); // warm-up
            sum += traverse_set(i);
        }
        thresholds[i] = (sum / 500) * 13 / 10;
    }
}

// Check if set 8 is high consistently (majority of checks)
int signal_high() {
    int high_count = 0;
    const int CHECKS = 5;
    for(int i=0;i<CHECKS;i++){
        if(traverse_set(8) > thresholds[8]) high_count++;
        for(volatile int w=0; w<100; w++);
    }
    return high_count >= 3;
}

// Wait for a stable high signal on set 8
void wait_for_start() {
    const int REQUIRED_HIGH = 5;
    int count = 0;
    while(1) {
        if(signal_high()) count++;
        else count = 0;
        for(volatile int w=0; w<1000; w++);
        if(count >= REQUIRED_HIGH) return; // stable high detected
    }
}

// Wait for a stable drop on set 8
void wait_for_drop() {
    const int REQUIRED_LOW = 5;
    int lows = 0;
    while(lows < REQUIRED_LOW){
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
        wait_for_start(); // only start after stable high

        int bits[8] = {0};
        const int MAX_SAMPLES = 100;

        for(int sample=0; sample<MAX_SAMPLES; sample++){
            if(!signal_high()) continue; // skip weak/noisy periods
            for(int b=0;b<8;b++){
                if(traverse_set(b) > thresholds[b]) bits[b]++;
            }
            for(volatile int w=0; w<3000; w++);
        }

        int value = 0;
        for(int b=0;b<8;b++){
            if(bits[b] > MAX_SAMPLES/2) value |= 1<<b;
        }
        printf("%d\n", value);

        wait_for_drop(); // wait for stable drop before next byte
    }
}