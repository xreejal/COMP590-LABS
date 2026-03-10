#include "util.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define BUF_SZ (1<<21)
#define L2_ASSOC 16
#define STRIDE (1<<16)
#define SET_STEP 32
#define START_SET 64

typedef struct entry {
    struct entry *next;
    char filler[64 - sizeof(struct entry*)];
} entry;

void *region;
entry *set_heads[9];
uint64_t cutoffs[9];

static inline uint64_t get_cycles(){
    uint32_t lo,hi;
    asm volatile("rdtscp":"=a"(lo),"=d"(hi)::"rcx");
    return ((uint64_t)hi<<32)|lo;
}

void init_set(int idx){
    char *base = (char*)region;
    entry *last = NULL;

    for(int i=0;i<L2_ASSOC;i++){
        entry *node = (entry*)(base + (START_SET + idx*SET_STEP)*64 + i*STRIDE);

        if(last) last->next = node;
        else set_heads[idx] = node;

        last = node;
    }

    last->next = NULL;
}

void warmup(int idx){
    entry *p = set_heads[idx];
    while(p) p = p->next;
}

uint64_t measure(int idx){
    uint64_t start = get_cycles();

    entry *p = set_heads[idx];
    while(p) p = p->next;

    return get_cycles() - start;
}

void determine_thresholds(){
    for(int s=0;s<9;s++){

        uint64_t total = 0;

        for(int i=0;i<800;i++){
            warmup(s);
            total += measure(s);
        }

        uint64_t avg = total/800;

        cutoffs[s] = avg * 2;
    }
}

int main(){

    srand(time(NULL));

    region = mmap(NULL, BUF_SZ,
        PROT_READ|PROT_WRITE,
        MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,
        -1,0);

    if(region==(void*)-1){
        perror("mmap");
        exit(1);
    }

    *((char*)region)=1;

    for(int i=0;i<9;i++)
        init_set(i);

    determine_thresholds();

    printf("Please press enter\n");
    getchar();

    printf("Receiver now listening\n");

    while(1){

        warmup(8);

        for(volatile int w=0; w<2000; w++);

        if(measure(8) > cutoffs[8]){

            int bit_votes[8]={0};
            int active_samples = 0;
            int attempts = 120;

            for(int s=0;s<attempts;s++){

                for(int j=0;j<9;j++)
                    warmup(j);

                for(volatile int delay=0; delay<5000; delay++);

                if(measure(8) > cutoffs[8]){

                    active_samples++;

                    for(int b=0;b<8;b++){
                        if(measure(b) > cutoffs[b])
                            bit_votes[b]++;
                    }
                }
            }

            if(active_samples > attempts/2){

                int decoded = 0;

                for(int b=0;b<8;b++){
                    if(bit_votes[b] > active_samples*0.7)
                        decoded |= (1<<b);
                }

                printf("%d\n",decoded);
            }

            int quiet=0;

            while(quiet<10){

                warmup(8);

                for(volatile int d=0; d<5000; d++);

                if(measure(8) < cutoffs[8])
                    quiet++;
                else
                    quiet=0;
            }
        }
    }
}