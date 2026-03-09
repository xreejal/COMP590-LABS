#include"util.h"
#include <sys/mman.h>

#define BUFF_SIZE (1<<21)
#define L2_WAYS 16
#define THRESHOLD 125  // Increased threshold to reduce false positives

int main(int argc, char **argv)
{
    void *buf = mmap(NULL, BUFF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

    if (buf == (void*) -1) {
        perror("mmap error");
        exit(EXIT_FAILURE);
    }

    for (uint64_t i = 0; i < BUFF_SIZE; i += 64) {
    	*((char*)buf + i) = 1;
	}

    printf("Please press enter.\n");
    char text_buf[128];
    fgets(text_buf, sizeof(text_buf), stdin);

    // Warmup
    printf("Warming up...\n");
    volatile char tmp;
    for (int w = 0; w < 10; w++) {
        for (uint64_t i = 0; i < BUFF_SIZE; i += 64) {
            tmp = *((char *)buf + i);
        }
    }

    printf("Receiver now listening.\n");

    // Track detection counts and total latency for each value
    int detection_counts[256] = {0};
    int total_latency[256] = {0};
    int total_rounds = 0;

    while (1) {
        total_rounds++;

        // Test all 256 possible values in RANDOM order to avoid bias
        int test_order[256];
        for (int i = 0; i < 256; i++) test_order[i] = i;

        // Simple shuffle
        for (int i = 255; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = test_order[i];
            test_order[i] = test_order[j];
            test_order[j] = tmp;
        }

        for (int idx = 0; idx < 256; idx++) {
            int val = test_order[idx];
            int target_set = (val * 4) % 1024;

            // PRIME: Fill our addresses for this set
            // This is EXACTLY what test_detect does
            for (int way = 0; way < L2_WAYS; way++) {
                uint64_t offset = (target_set * 64);
                tmp = *((char *)buf + offset);
            }

            // Shorter delay to scan faster
            for (volatile long i = 0; i < 20000000; i++);

            // PROBE: Measure latency
            uint64_t total_time = 0;
            for (int way = 0; way < L2_WAYS; way++) {
                uint64_t offset = (target_set * 64);
                uint64_t time = measure_one_block_access_time((uint64_t)buf + offset);
                total_time += time;
            }

            int avg = total_time / L2_WAYS;

            // If evicted, count this value and track latency
            if (avg > THRESHOLD) {
                detection_counts[val]++;
                total_latency[val] += avg;
            }
        }

        // Show progress
        printf("Round %d complete\n", total_rounds);

        // Every round, check if we have a winner
        // Use detection count first, then average latency as tie-breaker
        int max_count = 0;
        int winner = -1;
        int winner_avg_lat = 0;

        for (int v = 0; v < 256; v++) {
            int avg_lat = detection_counts[v] > 0 ? total_latency[v] / detection_counts[v] : 0;

            if (detection_counts[v] > max_count ||
                (detection_counts[v] == max_count && avg_lat > winner_avg_lat)) {
                max_count = detection_counts[v];
                winner = v;
                winner_avg_lat = avg_lat;
            }
        }

        // Show top 5 detected values
        printf("  Top detections: ");
        for (int i = 0; i < 5; i++) {
            int max_c = 0;
            int max_v = -1;
            for (int v = 0; v < 256; v++) {
                if (detection_counts[v] > max_c) {
                    max_c = detection_counts[v];
                    max_v = v;
                }
            }
            if (max_c > 0) {
                printf("val=%d(cnt=%d) ", max_v, max_c);
                detection_counts[max_v] = -999; // Mark as shown
            }
        }
        // Restore counts
        for (int v = 0; v < 256; v++) {
            if (detection_counts[v] == -999) detection_counts[v] = max_count;
        }
        printf("\n");

        // If a value detected consistently (in at least 80% of last 5 rounds), declare winner
        if (total_rounds >= 5 && max_count >= 4) {
            printf("\n>>> RECEIVED: %d (detected %d/%d times, avg_lat=%d) <<<\n\n",
                   winner, max_count, total_rounds, winner_avg_lat);
            fflush(stdout);

            // Reset
            for (int v = 0; v < 256; v++) {
                detection_counts[v] = 0;
                total_latency[v] = 0;
            }
            total_rounds = 0;

            // Cooldown
            for (volatile long i = 0; i < 500000000; i++);
        }
    }

    munmap(buf, BUFF_SIZE);
    return 0;
}