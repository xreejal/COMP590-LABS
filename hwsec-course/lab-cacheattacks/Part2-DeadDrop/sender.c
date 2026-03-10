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
#define SLOT_DELAY 8000

void *buf;
char *set_addrs[L2_WAYS];

static inline void delay() {
    for(volatile int i=0;i<SLOT_DELAY;i++);
}

void build_set() {
    char *base = (char*)buf;

    for(int i=0;i<L2_WAYS;i++){
        set_addrs[i] = base + TARGET_SET*64 + i*STRIDE;
    }
}

void evict_set(){
    for(int i=0;i<L2_WAYS;i++){
        *(volatile char*)set_addrs[i];
    }
}

void send_bit(int bit){
    if(bit){
        for(int i=0;i<200;i++)
            evict_set();
    }
    delay();
}

void send_byte(int value){
    printf("[DEBUG] Sending byte: %d\n", value);
    fflush(stdout);

    for(int i=0;i<8;i++){
        int bit = (value >> i) & 1;
        send_bit(bit);
    }

    printf("[DEBUG] Byte %d sent successfully\n", value);
    fflush(stdout);
}

void send_sync(){
    for(int i=0;i<50000;i++)
        evict_set();

    printf("[DEBUG] Sent sync signal\n");
    fflush(stdout);
}

int main(){

    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
               MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB,
               -1,0);

    if(buf==(void*)-1){
        perror("mmap");
        exit(1);
    }

    *((char*)buf) = 1;

    build_set();

    printf("Please type a message (0-255 per line).\n");

    char line[128];

    while(fgets(line,sizeof(line),stdin)){

        int value = atoi(line);

        if(value < 0 || value > 255){
            printf("Enter value 0-255\n");
            continue;
        }

        send_sync();      // start-of-message signal
        send_byte(value); // send the data
    }

    return 0;
}