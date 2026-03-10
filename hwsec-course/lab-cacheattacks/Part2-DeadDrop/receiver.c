#include "util.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16
#define STRIDE (1<<16)

#define TARGET_SET 128
#define SLOT_DELAY 12000

void *buf;
char *set_addrs[L2_WAYS];

uint64_t threshold;

static inline uint64_t rdtscp(){
    uint32_t lo,hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
    return ((uint64_t)hi<<32)|lo;
}

static inline void delay(){
    for(volatile int i=0;i<SLOT_DELAY;i++);
}

void build_set(){
    char *base = (char*)buf;

    for(int i=0;i<L2_WAYS;i++){
        set_addrs[i] = base + TARGET_SET*64 + i*STRIDE;
    }
}

void prime_set(){
    for(int i=0;i<L2_WAYS;i++){
        *(volatile char*)set_addrs[i];
    }
}

uint64_t probe_set(){

    uint64_t start = rdtscp();

    for(int i=0;i<L2_WAYS;i++){
        *(volatile char*)set_addrs[i];
    }

    return rdtscp() - start;
}

void calibrate() {
    uint64_t sum = 0;
    int samples = 1000;

    for(int i=0;i<samples;i++){
        prime_set();
        sum += probe_set();
    }

    uint64_t avg = sum / samples;
    threshold = avg + avg/2; // 50% higher than average idle
    printf("Calibrated threshold: %llu\n", (unsigned long long)threshold);
}

int receive_bit(){

    prime_set();

    delay();

    uint64_t t = probe_set();

    if(t > threshold)
        return 1;
    else
        return 0;
}

int receive_byte(){

    int value = 0;

    for(int i=0;i<8;i++){

        int bit = receive_bit();

        value |= (bit << i);
    }

    return value;
}

int detect_signal() {
    int consecutive_high = 0;
    int required_high = 50;  // need 50 consecutive high-latency samples

    for(int i=0;i<1000;i++){  // check up to 1000 slots
        prime_set();
        delay();

        if(probe_set() > threshold){
            consecutive_high++;
            if(consecutive_high >= required_high)
                return 1;  // signal detected
        } else {
            consecutive_high = 0;
        }
    }

    return 0;  // no signal
}

int main() {

    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
               MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,
               -1,0);

    if(buf == (void*)-1){
        perror("mmap");
        exit(1);
    }

    *((char*)buf) = 1;

    build_set();

    calibrate();

    printf("Please press enter.\n");
    getchar();

    printf("Receiver now listening.\n");

    while(1){
        // Wait for a signal from sender
        if(detect_signal()){
            int value = receive_byte();
            printf("%d\n", value);
        } else {
            // Optional: short sleep to reduce CPU usage
            for(volatile int i=0;i<1000;i++);
        }
    }

    return 0;
}