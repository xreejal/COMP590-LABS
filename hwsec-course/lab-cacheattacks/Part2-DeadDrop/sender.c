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

typedef struct entry{
    struct entry *next;
    char filler[64 - sizeof(struct entry*)];
} entry;

void *region;
entry *channels[9];

void setup_channel(int id){

    char *base = (char*)region;
    entry *prev = NULL;

    for(int i=0;i<L2_ASSOC;i++){

        entry *node = (entry*)(base + (START_SET + id*SET_STEP)*64 + i*STRIDE);

        if(prev)
            prev->next = node;
        else
            channels[id] = node;

        prev = node;
    }

    prev->next = NULL;
}

void disturb(int idx){

    entry *p = channels[idx];

    while(p) p = p->next;

    p = channels[idx];

    while(p) p = p->next;
}

void transmit_bits(int value){

    int order[8];

    for(int i=0;i<8;i++)
        order[i]=i;

    for(int i=7;i>0;i--){
        int j = rand()%(i+1);
        int t = order[i];
        order[i]=order[j];
        order[j]=t;
    }

    for(int k=0;k<8;k++){

        int bit = order[k];

        if((value>>bit)&1)
            disturb(bit);
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
        setup_channel(i);

    printf("Please type a message\n");

    char input[128];

    while(fgets(input,sizeof(input),stdin)){

        int num = atoi(input);

        if(num<0 || num>255){
            printf("Enter number 0-255\n");
            continue;
        }

        printf("Sending %d\n",num);

        long loops = 2000000;

        while(loops--){

            disturb(8);           // valid signal

            transmit_bits(num);   // payload
        }

        printf("Transmission complete\n");
    }

    return 0;
}