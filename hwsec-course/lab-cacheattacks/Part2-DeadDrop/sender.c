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
    char pad[64 - sizeof(struct entry*)];
} entry;

void *region;
entry *channels[9];
uint64_t loops_per_message = 1500000;

// Shuffle helper
void shuffle_array(int *arr, int n){
    for(int i=n-1;i>0;i--){
        int j = rand()%(i+1);
        int t = arr[i]; arr[i]=arr[j]; arr[j]=t;
    }
}

// Setup a channel with optional shuffle of eviction nodes
void setup_channel(int id){
    char *base = (char*)region;
    entry *nodes[L2_ASSOC];
    
    for(int i=0;i<L2_ASSOC;i++)
        nodes[i] = (entry*)(base + (START_SET + id*SET_STEP)*64 + i*STRIDE);

    shuffle_array((int*)nodes, L2_ASSOC); // Shuffle nodes differently
    for(int i=0;i<L2_ASSOC-1;i++)
        nodes[i]->next = nodes[i+1];
    nodes[L2_ASSOC-1]->next = NULL;

    channels[id] = nodes[0];
}

// Disturb a channel (evict)
void disturb(int id){
    entry *p = channels[id];
    while(p) p = p->next;
    // Optional second pass to increase eviction
    p = channels[id];
    while(p) p = p->next;
}

// Transmit 8 bits
void transmit_bits(int value){
    int bits[8]={0,1,2,3,4,5,6,7};
    shuffle_array(bits,8); // shuffle bit order each transmission

    for(int i=0;i<8;i++){
        int b = bits[i];
        if((value>>b)&1) disturb(b);
    }
}

int main(){
    srand(time(NULL));

    region = mmap(NULL, BUF_SZ,
                  PROT_READ|PROT_WRITE,
                  MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,
                  -1,0);

    if(region==(void*)-1){ perror("mmap"); exit(1); }

    *((char*)region)=1;

    for(int i=0;i<9;i++) setup_channel(i);

    printf("Please type a message.\n");

    char input[128];
    while(fgets(input,sizeof(input),stdin)){
        int msg = atoi(input);
        if(msg<0||msg>255){ printf("Enter number 0-255\n"); continue; }

        printf("Sending %d\n", msg);

        long loops = loops_per_message;
        while(loops--){
            disturb(8);           // valid signal
            transmit_bits(msg);   // payload
        }

        printf("Transmission complete\n");
    }

    return 0;
}