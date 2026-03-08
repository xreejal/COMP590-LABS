#include "util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
static inline uint64_t read_tsc(void)
{
    unsigned int aux = 0;
    unsigned int lo;
    unsigned int hi;

    asm volatile("mfence\n\tlfence\n\trdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void serialise_cpu(void)
{
    asm volatile("lfence\n\t" : : : "memory");
}
#else
static inline uint64_t read_tsc(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void serialise_cpu(void)
{
    (void)0;
}
#endif

/* Measure the time it takes to access a block with virtual address addr. */
CYCLES measure_one_block_access_time(ADDR_PTR addr)
{
    serialise_cpu();
    uint64_t start = read_tsc();

    uint8_t value = *(volatile uint8_t *)addr;

    serialise_cpu();
    uint64_t end = read_tsc();

    (void)value;
    return (CYCLES)(end - start);
}

/*
 * CLFlushes the given address.
 *
 * Note: clflush is provided to help you debug and should not be used in your
 * final submission.
 */
void clflush(ADDR_PTR addr)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    asm volatile("clflush (%0)\n\t" : : "r"(addr) : "memory");
    asm volatile("mfence\n\t" : : : "memory");
#else
    (void)addr;
#endif
}

/* Converts a string to its binary representation. */
char *string_to_binary(char *s)
{
  if (s == NULL)
    return 0; /* no input string */

  size_t len = strlen(s);

  // Each char is one byte (8 bits) and + 1 at the end for null terminator
  char *binary = malloc(len * 8 + 1);
  if (!binary) {
    return NULL;
  }
  binary[len * 8] = '\0';

  for (size_t i = 0; i < len; ++i)
  {
    uint8_t ch = (uint8_t)s[i];
    for (int j = 7; j >= 0; --j)
    {
      binary[i * 8 + (7 - j)] = (ch & (1u << j)) ? '1' : '0';
    }
  }

  return binary;
}

/* Converts a binary string to its ASCII representation. */
char *binary_to_string(char *data)
{
  if (data == NULL) {
    return NULL;
  }

  // Each char is 8 bits
  size_t msg_len = strlen(data) / 8;

  // Add one for null terminator at the end
  char *msg = malloc(msg_len + 1);
  if (!msg) {
    return NULL;
  }
  msg[msg_len] = '\0';

  for (size_t i = 0; i < msg_len; i++)
  {
    char tmp[9] = {0};

    for (int j = 0; j < 8; j++)
    {
      tmp[j] = data[i * 8 + j];
    }

    msg[i] = (char)strtol(tmp, NULL, 2);
  }

  return msg;
}

/* Converts a string to integer */
int string_to_int(char* s)
{
  return atoi(s);
}
