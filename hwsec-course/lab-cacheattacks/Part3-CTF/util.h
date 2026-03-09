#ifndef PART3_UTIL_H_
#define PART3_UTIL_H_

#include <stdint.h>

#define ADDR_PTR uint64_t
#define CYCLES uint32_t

CYCLES measure_one_block_access_time(ADDR_PTR addr);
void clflush(ADDR_PTR addr);

#endif
