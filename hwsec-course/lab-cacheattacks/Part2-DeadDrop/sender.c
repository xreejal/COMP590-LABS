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

void *buffer;
struct node *l2_sets[9];

// Build a linked list for one set
// Best working version
void create_set(int set_idx) {
    char *base = (char*)buffer;
    struct node *prev = NULL;

    for(int i=0; i<L2_WAYS; i++){
        struct node *n = (struct node*)(base + (BASE_SET + set_idx*SET_SPACING)*64 + i*STRIDE);
        if(prev) prev->next = n;
        else l2_sets[set_idx] = n;
        prev = n;
    }
    prev->next = NULL;
}

// Traverse a set to evict its cache lines
void evict_set(int idx){
    struct node *curr = l2_sets[idx];
    while(curr) curr = curr->next;
    // Second pass for reliability
    curr = l2_sets[idx];
    while(curr) curr = curr->next;
}

// Evict only the data bits (0–7) in random order
void evict_data_bits_random(int value){
    int bits[8];
    for(int i=0;i<8;i++) bits[i] = i;

    // Fisher-Yates shuffle
    for(int i=7;i>0;i--){
        int j = rand() % (i+1);
        int tmp = bits[i]; bits[i] = bits[j]; bits[j] = tmp;
    }

    for(int i=0;i<8;i++){
        int b = bits[i];
        if((value >> b) & 1) evict_set(b);
    }
}

int main() {
    srand(time(NULL));

    buffer = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
                  MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB, -1, 0);

    if(buffer==(void*)-1){ perror("mmap"); exit(1); }

    *((char*)buffer) = 1;

    for(int i=0;i<=8;i++) create_set(i);

    printf("Sender ready.\n");

    char line[128];
    while(fgets(line,sizeof(line),stdin)){
        int value = atoi(line);
        if(value<0 || value>255){ 
            printf("Enter a value between 0 and 255\n"); 
            continue; 
        }

        printf("Sending %d\n", value);

        long iterations = 2000000;
        while(iterations--){
            evict_set(8);             // Valid bit
            evict_data_bits_random(value); // Data bits
        }

        printf("Sent.\n");
    }

    return 0;
}