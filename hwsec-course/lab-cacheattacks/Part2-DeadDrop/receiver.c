#include "util.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16
#define STRIDE (1<<16)

#define DATA_SETS 8       // sets 0-7 carry bits
#define SIGNAL_SET 8      // set 8 for sync
#define SLOT_DELAY 4000
//A

void *buf;
char *sets[DATA_SETS + 1];  // pointers to each set
uint64_t thresholds[DATA_SETS + 1]; // per-set thresholds

static inline uint64_t rdtscp(){
    uint32_t lo,hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
    return ((uint64_t)hi<<32)|lo;
}

static inline void delay(){
    for(volatile int i=0;i<SLOT_DELAY;i++);
}

void build_sets(){
    char *base = (char*)buf;
    for(int s=0; s<=DATA_SETS; s++){
        sets[s] = base + (64*64) + s*STRIDE;  // start + s*stride
    }
}

void prime_set(int s){
    for(int i=0;i<L2_WAYS;i++)
        *(volatile char*)(sets[s] + i*STRIDE);
}

uint64_t probe_set(int s){
    uint64_t start = rdtscp();
    for(int i=L2_WAYS-1;i>=0;i--)
        *(volatile char*)(sets[s] + i*STRIDE);
    return rdtscp() - start;
}

void calibrate(uint64_t manual_threshold){
    for(int s=0; s<=DATA_SETS; s++){

        uint64_t hit=0, miss=0;

        for(int i=0;i<1000;i++){
            prime_set(s);
            hit += probe_set(s);
        }

        for(int i=0;i<1000;i++){
            miss += probe_set(s);
        }

        hit /= 1000;
        miss /= 1000;

        thresholds[s] = (hit + miss) / 2;

        // <-- print INSIDE the loop
        printf("Set %d threshold: %llu (hit=%llu miss=%llu)\n",
            s,
            (unsigned long long)thresholds[s],
            (unsigned long long)hit,
            (unsigned long long)miss);
    }
}
// Receive one byte with multi-sample voting
int receive_byte(){
    int bit_counts[DATA_SETS] = {0};
    int samples = 50;
    int valid_samples = 0;

    for(int s=0; s<samples; s++){
        // Prime all sets
        for(int d=0; d<=DATA_SETS; d++) prime_set(d);
        delay();

        uint64_t t_signal = probe_set(SIGNAL_SET);
        if(t_signal > thresholds[SIGNAL_SET]){
            valid_samples++;
            for(int d=0; d<DATA_SETS; d++){
                if(probe_set(d) > thresholds[d]){
                    bit_counts[d]++;
                }
            }
        }
    }

    int value=0;
    for(int d=0; d<DATA_SETS; d++){
        if(bit_counts[d] > (valid_samples*0.7))  // >70% voting
            value |= (1<<d);
    }

    return value;
}

// Wait for signal to appear (sync)
void wait_for_signal(){
    while(1){
        prime_set(SIGNAL_SET);
        delay();
        if(probe_set(SIGNAL_SET) > thresholds[SIGNAL_SET])
            break;
    }
}

int main(int argc, char **argv){
    srand(time(NULL));

    uint64_t manual_threshold = 0;
    if(argc>1)
        manual_threshold = strtoull(argv[1], NULL, 10);

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
               MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,
               -1,0);
    if(buf==(void*)-1){ perror("mmap"); exit(1); }
    *((char*)buf)=1;

    build_sets();
    calibrate(manual_threshold);

    printf("Press enter to start receiver.\n");
    getchar();
    printf("Receiver listening...\n");

    while(1){

    /* WAIT FOR SYNC */
    while(1){
        prime_set(SIGNAL_SET);
        delay();
        uint64_t t = probe_set(SIGNAL_SET);

        if(t > thresholds[SIGNAL_SET]) {
            break;   // sync detected
        }
    }

    int value = 0;

    /* RECEIVE 8 BITS */
    for(int i=0;i<8;i++){

        prime_set(i);
        delay();

        uint64_t t = probe_set(i);

        if(t > thresholds[i])
            value |= (1 << i);
    }

    printf("[DEBUG] Received byte: %d\n", value);
}

    return 0;
}