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
#define SLOT_DELAY 4000

void *buf;
char *set_addrs[L2_WAYS+1]; // +1 for signal set

static inline void delay(){
    for(volatile int i=0;i<SLOT_DELAY;i++);
}

void build_set(){
    char *base = (char*)buf;
    for(int i=0;i<L2_WAYS;i++)
        set_addrs[i] = base + TARGET_SET*64 + i*STRIDE;
    
    // Add signal set (set 8) at an offset
    set_addrs[8] = base + (TARGET_SET+1)*64 + 0*STRIDE;
}

// Send a single bit using eviction of the corresponding set
void send_bit(int bit, int set_index){
    if(bit){
        for(int i=0;i<2000;i++)  // strong eviction for reliability
            *(volatile char*)set_addrs[set_index];
    }
    delay();
}

// Evict signal set to indicate start of byte
void send_sync(){
    for(int i=0;i<20000;i++)  // longer signal for receiver alignment
        *(volatile char*)set_addrs[8];
    printf("[DEBUG] Sent sync signal\n");
    fflush(stdout);
}

// Send a byte (sets 0-7 = data, 8 = signal)
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

    build_set();

    printf("Enter message (0-255 per line):\n");

    char line[128];
    while(fgets(line,sizeof(line),stdin)){
        int value = atoi(line);
        if(value<0 || value>255){ printf("Enter 0-255\n"); continue; }

        send_byte(value); // send actual byte
    }

    return 0;
}