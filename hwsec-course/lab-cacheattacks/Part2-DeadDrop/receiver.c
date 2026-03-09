#include "util.h"
#include <sys/mman.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16
#define STRIDE (1<<16)
#define SET_SPACING 32
#define BASE_SET 64

struct node {
    struct node *next;
    char pad[64 - sizeof(struct node*)];
};

void *buf;
struct node *sets[9];
uint64_t thresholds[9];

// RDTSCP timing
static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

void shuffle(struct node **array, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        struct node *tmp = array[i];
        array[i] = array[j];
        array[j] = tmp;
    }
}

void build_set(int idx) {
    char *base = (char*)buf;
    struct node *nodes[L2_WAYS];
    int phys = BASE_SET + idx * SET_SPACING;

    for (int i = 0; i < L2_WAYS; i++)
        nodes[i] = (struct node*)(base + phys*64 + i*STRIDE);

    shuffle(nodes, L2_WAYS);

    for (int i = 0; i < L2_WAYS-1; i++)
        nodes[i]->next = nodes[i+1];
    nodes[L2_WAYS-1]->next = NULL;

    sets[idx] = nodes[0];
}

void prime_set(int idx) {
    struct node *p = sets[idx];
    while (p) p = p->next;
}

uint64_t probe_set(int idx) {
    uint64_t start = rdtscp();
    struct node *p = sets[idx];
    while (p) p = p->next;
    return rdtscp() - start;
}

void calibrate() {
    for (int i = 0; i <= 8; i++) {
        uint64_t sum = 0;
        for (int j = 0; j < 1000; j++) {
            prime_set(i);
            sum += probe_set(i);
        }
        thresholds[i] = sum * 2 / 1; // ~2x average, no printing
    }
}

// Stable signal detection
int signal_high() {
    int high_count = 0;
    const int CHECKS = 5;
    for (int i = 0; i < CHECKS; i++) {
        if (probe_set(8) > thresholds[8]) high_count++;
        for (volatile int w = 0; w < 100; w++);
    }
    return high_count >= 3;
}

// Decode byte using majority vote
int decode_byte(int bit_counts[8], int valid_samples) {
    int val = 0;
    for (int i = 0; i < 8; i++)
        if (bit_counts[i] > valid_samples*75/100)
            val |= 1 << i;
    return val;
}

int main() {
    srand(time(NULL));

    buf = mmap(NULL, BUFF_SIZE, PROT_READ|PROT_WRITE,
               MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB, -1, 0);
    if (buf == (void*)-1) { perror("mmap"); exit(1); }
    *((char*)buf) = 1;

    for (int i = 0; i <= 8; i++)
        build_set(i);

    calibrate();

    printf("Please press enter.\n");
    getchar();
    printf("Receiver now listening...\n");

    while (1) {
        // Wait for start signal
        while (!signal_high()) for (volatile int w = 0; w < 1000; w++);

        // Sample a byte
        int bit_counts[8] = {0};
        int valid_samples = 0;
        int samples = 100;
        int s = 0;

        while (s < samples) {
            for (int i = 0; i <= 8; i++)
                prime_set(i);
            for (volatile int k = 0; k < 5000; k++);

            if (probe_set(8) > thresholds[8]) {
                valid_samples++;
                for (int i = 0; i < 8; i++)
                    if (probe_set(i) > thresholds[i])
                        bit_counts[i]++;
            }
            s++;
        }

        if (valid_samples > samples/2) {
            int value = decode_byte(bit_counts, valid_samples);
            printf("%d\n", value);
        }

        // Wait for signal to drop consistently
        int lows = 0;
        while (lows < 10) {
            prime_set(8);
            for (volatile int i = 0; i < 5000; i++);
            if (probe_set(8) < thresholds[8])
                lows++;
            else
                lows = 0;
        }
    }

    return 0;
}