#include "util.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16
#define STRIDE (1<<16)

#define DATA_SETS 8
#define SIGNAL_SET 8
#define SLOT_DELAY 4000

void *buf;
char *sets[DATA_SETS+1]; // 0-7 = data bits, 8 = sync

static inline void delay(){
    for(volatile int i=0;i<SLOT_DELAY;i++);
}

// Map sender sets to same offsets as receiver
void build_sets(){
    char *base = (char*)buf;
    for(int s=0; s<=DATA_SETS; s++){
        sets[s] = base + (64*64) + s*STRIDE;
    }
}

void send_bit(int bit, int set_index){
    if(bit){
        for(int i=0;i<L2_WAYS;i++)   // <- only 16 ways
            *(volatile char*)(sets[set_index] + i*STRIDE);
    }
    delay();
}

void send_sync(){
    for(int i=0;i<L2_WAYS*100;i++)  // small repeated eviction for signal
        *(volatile char*)(sets[SIGNAL_SET] + (i % L2_WAYS)*STRIDE);
    printf("[DEBUG] Sent sync signal\n");
    fflush(stdout);
}

// Send a byte (sets 0-7 = data bits)
void send_byte(int value){
    printf("[DEBUG] Sending byte: %d\n", value);
    fflush(stdout);

    send_sync();

    for(int i=0;i<8;i++){
        int bit = (value >> i) & 1;
        send_bit(bit, i);
    }

    printf("[DEBUG] Byte %d sent successfully\n", value);
    fflush(stdout);

    // short pause to avoid overlap
    for(int i=0;i<50000;i++) delay();
}

int main(){
    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
               MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,
               -1,0);
    if(buf==(void*)-1){ perror("mmap"); exit(1); }
    *((char*)buf)=1;

    build_sets();

    printf("Enter message (0-255 per line):\n");

    char line[128];
    while(fgets(line,sizeof(line),stdin)){
        int value = atoi(line);
        if(value<0 || value>255){ printf("Enter 0-255\n"); continue; }

        send_byte(value); // send actual byte
    }

    return 0;
}