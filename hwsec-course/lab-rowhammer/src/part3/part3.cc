#include <algorithm>
#include <array>
#include <tuple>
#include <ranges>
#include <numeric>

#include "../shared.hh"
#include "../params.hh"
#include "../util.hh"

#define BANKS 16
#define CONSISTENCY_RATE 0.90
// TODO: Threshold derived in part2
#define THRESHOLD 400
#define POOL_SIZE 5000
#define ROUNDS  100


//Show Fast forward merge
//Verify part 3 working

void print_bins(const std::array<std::vector<uint64_t>, BANKS>& bins) {
    for (size_t i = 0; i < BANKS; i++) {
        const auto& bin = bins[i];
        printf("Bin[%zu] size = %zu\n", i, bin.size());
        for (size_t j = 0; j < bin.size(); j++) {
            printf("  [%3zu] phys = 0x%lx\n", j, bin[j]);
        }
        printf("\n");
    }
}

int gt(const void * a, const void * b) {
   uint64_t val_a = *(uint64_t*)a;
   uint64_t val_b = *(uint64_t*)b;
   return (val_a < val_b) ? -1 : (val_a > val_b) ? 1 : 0;
}

uint64_t median(uint64_t* vals, size_t size) {
	qsort(vals, size, sizeof(uint64_t), gt);
	return ((size%2)!=0) ? vals[size/2] : (vals[(size_t)size/2-1]+vals[((size_t)size/2)])/2;
}

/*
 * bin_rows
 *
 * Bins a selection of addresses in the range of [starting_addr, final_addr)
 * based on measure_bank_latency.
 *
 * Input: starting and ending addresses
 * Output: An array of 16 vectors, each of which holds addresses that share the same bank
 *
 * HINT: You can refer the way in part2.cc: run measure_bank_latency for multiple times and then get the median number to avoid noise
 *
 */

std::array<std::vector<uint64_t>, BANKS> bin_rows(uint64_t starting_addr, uint64_t final_addr) {
    // TODO - Exercise 3-1
    std::array<std::vector<uint64_t>, BANKS> bins;
    std::vector<uint64_t> candidates;

    // Step 1: sample addresses
    for (uint64_t addr = starting_addr;
         addr < final_addr;
         addr += CACHELINE_SIZE * 64) {

        candidates.push_back(addr);
        if (candidates.size() >= POOL_SIZE) break;
    }

    // Step 2: clustering
    for (size_t addr_idx = 0; addr_idx < candidates.size(); addr_idx++) {
        auto addr = candidates[addr_idx];

        int best_bin = -1;
        uint64_t best_med = 0;

        // try matching against existing bins
        for (int b = 0; b < BANKS; b++) {

            if (bins[b].empty()) continue;

            uint64_t ref = bins[b][0];  // deterministic reference (more stable than rand)

            uint64_t tmp[ROUNDS];

            for (int i = 0; i < ROUNDS; i++) {
                tmp[i] = measure_bank_latency(
                    (volatile char*)addr,
                    (volatile char*)ref
                );
            }

            uint64_t med = median(tmp, ROUNDS);

            if (med > THRESHOLD && med > best_med) {
                best_med = med;
                best_bin = b;
            }
        }

        // DEBUG: print first few addresses to see what's happening
        if (addr_idx < 10) {
            printf("Address %zu: best_bin=%d, best_med=%lu, threshold=%d\n",
                   addr_idx, best_bin, best_med, THRESHOLD);
        }

        // assign to best bin OR create new one
        if (best_bin != -1) {
            bins[best_bin].push_back(addr);
        } else {
            bool placed = false;

            // seed new bin if possible
            for (int b = 0; b < BANKS; b++) {
                if (bins[b].empty()) {
                    bins[b].push_back(addr);
                    placed = true;
                    break;
                }
            }

            // if no empty bin and no latency match, discard this address
            (void)placed;
        }
    }

    // Step 3: sanity check and debug output
    printf("\n=== Bin Distribution ===\n");
    printf("Total candidates sampled: %zu\n", candidates.size());
    int total_binned = 0;
    for (int i = 0; i < BANKS; i++) {
        printf("Bin[%d]: %zu addresses\n", i, bins[i].size());
        total_binned += bins[i].size();
    }
    printf("Total binned: %d / %zu (%.1f%%)\n", total_binned, candidates.size(),
           100.0 * total_binned / candidates.size());
    printf("\n");

    return bins;
}

/*
 *
 * DO NOT MODIFY BELOW ME
 *
 */

std::tuple<uint64_t, double> get_most_frequent(const std::vector<uint64_t>& data) {
    std::map<uint64_t, uint64_t> freq_map;

    for (const auto& item : data) {
        freq_map[item]++;
    }

    auto [most_freq, max_count] = std::accumulate(
        freq_map.begin(),
        freq_map.end(),
        std::pair<uint64_t, uint64_t>{0, 0},
        [](const auto& best, const auto& current) {
            return current.second > best.second ? 
                std::pair{current.first, current.second} : best;
        }
    );

    return {most_freq, static_cast<double>(max_count) / data.size()};
}



template <size_t LEN>
std::optional<uint64_t> find_candidate_function(const std::array<std::vector<uint64_t>, LEN>& bins) {
    std::optional<uint64_t> result = std::nullopt;

    std::array<std::function<uint64_t(uint64_t)>, 3> functions = {
        [](uint64_t x) {
            return ((get_bit(x, 14) ^ get_bit(x, 17)) << 3) |
                   ((get_bit(x, 15) ^ get_bit(x, 18)) << 2) |
                   ((get_bit(x, 16) ^ get_bit(x, 19)) << 1) |
                   ((get_bit(x, 7) ^ get_bit(x, 8) ^ get_bit(x, 9) ^ get_bit(x, 12) ^ get_bit(x, 13) ^ get_bit(x, 15) ^ get_bit(x, 16)));
        },
        [](uint64_t x) {
            return ((get_bit(x, 15) ^ get_bit(x, 18)) << 3) |
                   ((get_bit(x, 16) ^ get_bit(x, 19)) << 2) |
                   ((get_bit(x, 17) ^ get_bit(x, 20)) << 1) |
                   ((get_bit(x, 7) ^ get_bit(x, 8) ^ get_bit(x, 9) ^ get_bit(x, 12) ^ get_bit(x, 13) ^ get_bit(x, 18) ^ get_bit(x, 19)));
        },

        [](uint64_t x) {
            return ((get_bit(x, 13) ^ get_bit(x, 17)) << 3) |
                   ((get_bit(x, 14) ^ get_bit(x, 18)) << 2) |
                   ((get_bit(x, 15) ^ get_bit(x, 19)) << 1) |
                   ((get_bit(x, 7) ^ get_bit(x, 8) ^ get_bit(x, 9) ^ get_bit(x, 12) ^ get_bit(x, 13) ^ get_bit(x, 20) ^ get_bit(x, 21)));
        },

    };

    for (size_t i = 0; i < functions.size(); i++) {
        auto &f = functions[i];
        bool good = true;
        double min_freq = 1.0;
        printf("\n=== Function F%zu ===\n", i);
        for (size_t bin_idx = 0; bin_idx < bins.size(); bin_idx++) {
            auto &bin = bins[bin_idx];
            if (bin.empty()) continue;

            uint64_t match_count = 0;
            std::vector<uint64_t> bank_comp(bin.size());
            std::transform(bin.begin(), bin.end(), bank_comp.begin(), f);
            auto [id, freq] = get_most_frequent(bank_comp);

            printf("Bin[%zu] (size=%zu): consistency=%.2f%% (bank_id=%lu)\n",
                   bin_idx, bin.size(), freq * 100.0, id);

            if (freq < CONSISTENCY_RATE) {
                good = false;
            }
            min_freq = std::min(min_freq, freq);
        }
        printf("Overall min consistency: %.2f%%\n", min_freq * 100.0);

        if (good) {
            // conflicting results
            if (result.has_value()) {
                printf("WARNING: Multiple valid functions found!\n");
                return std::nullopt;
            }
            result = i;
            printf("*** F%zu passes all bins! ***\n", i);
        }
    }

    return result;
}

int main (int ac, char **av) {
    
    setvbuf(stdout, NULL, _IONBF, 0);

    // Allocate a large pool of memory (of size BUFFER_SIZE_MB) pointed to
    // by allocated_mem
    allocated_mem = allocate_pages(BUFFER_SIZE_MB * 1024UL * 1024UL);
 
    // Setup PPN_VPN_map
    setup_PPN_VPN_map(allocated_mem, PPN_VPN_map);

    auto result = find_candidate_function(
        bin_rows((uint64_t)allocated_mem, (uint64_t)allocated_mem + ROW_SIZE * 4096));
  uint64_t victim = 0x96ec3000;                                                                                                                                                                   
     //Uses F0 to create a reference and checks every key to find which key maps properly     
     //as a result we get 6 for victim which maps perfectly to k=3      
                                                                                                                                                                                  
  auto f0 = [](uint64_t x) {                                                                                                                                                                      
      return ((get_bit(x,14) ^ get_bit(x,17)) << 3) |                                                                                                                                             
             ((get_bit(x,15) ^ get_bit(x,18)) << 2) |                                                                                                                                             
             ((get_bit(x,16) ^ get_bit(x,19)) << 1) |                                                                                                                                             
             ( get_bit(x,7)  ^ get_bit(x,8)  ^ get_bit(x,9) ^                                                                                                                                     
               get_bit(x,12) ^ get_bit(x,13) ^ get_bit(x,15) ^ get_bit(x,16));                                                                                                                    
  };                                                                                                                                                                                              
                                                                                                                                                                                                  
  uint64_t victim_bank = f0(victim);                                                                                                                                                              
  printf("\n=== Q3-2: Victim 0x%lx -> bank %lu ===\n", victim, victim_bank);
                                                                                                                                                                                                  
  uint64_t base = (victim + 0x20000) & ~0x1e000UL;  // row+1, bits[16:13] cleared                                                                                                                 
  base |= (victim & 0x1fff);                          // restore column                                                                                                                           
                                                                                                                                                                                                  
  for (int k = 0; k < 16; k++) {                        
      uint64_t cand = base | ((uint64_t)k << 13);                                                                                                                                                 
      uint64_t cand_bank = f0(cand);                                                                                                                                                              
      printf("k=%2d addr=0x%lx bank=%lu %s\n",
             k, cand, cand_bank,                                                                                                                                                                  
             cand_bank == victim_bank ? "<-- SAME BANK" : "");
  }  
    if (result.has_value()) {
        printf("Identified function %lu as correct\n", result.value());
    } else {
        puts("Did not identify a correct function :(");
    }
}