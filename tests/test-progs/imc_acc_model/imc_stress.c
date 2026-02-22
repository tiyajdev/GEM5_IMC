#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------
// 1. HARDWARE DEFINITIONS
// ---------------------------------------------------------
#define IMC_BASE    0x30000000
#define DMA_BASE    0x10000000  // Direct Physical Memory Base

#define HW_MAX_ROWS 32  // Input Length (Di * M)
#define HW_MAX_COLS 24   // Number of Filters (Do * NumMacros)

// Register Offsets
#define REG_SRC         0x00
#define REG_DST         0x08
#define REG_WGT         0x10
#define REG_SIZE        0x18
#define REG_CMD         0x20
#define REG_STATUS      0x28
#define REG_BANK        0x30
#define REG_ACTIVE_COLS 0x38
#define REG_ACTIVE_ROWS 0x40

#define IN_LEN 32
#define NUM_FILTERS 24
#define PRECISION_W 8 // 4-bit inputs
#define PRECISION_IN 8

#ifndef BATCH_SIZE
#define BATCH_SIZE 64
#endif
#define LOOP BATCH_SIZE

// Register Helpers
void reg_write(int offset, uint64_t val) {
    *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset) = val;
}
uint64_t reg_read(int offset) {
    return *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset);
}

#include <stdint.h>

#include <stdint.h>

// =========================================================
// GEM5 MAGIC OPS (LEGACY OPCODE 0x7B FIX)
// =========================================================

void m5_reset_stats(uint64_t delay, uint64_t period) {
    register uint64_t a0 asm("a0") = delay;
    register uint64_t a1 asm("a1") = period;
    
    // CHANGED: 0x73 -> 0x7B
    // Opcode 0x7B is the custom "M5Op" reserved opcode for your Gem5 build
    asm volatile (".insn r 0x7B, 0x0, 0x40, x0, %0, %1" :: "r"(a0), "r"(a1));
}

void m5_dump_stats(uint64_t delay, uint64_t period) {
    register uint64_t a0 asm("a0") = delay;
    register uint64_t a1 asm("a1") = period;
    
    // CHANGED: 0x73 -> 0x7B
    // 0x41 is the "Dump Stats" function code
    asm volatile (".insn r 0x7B, 0x0, 0x41, x0, %0, %1" :: "r"(a0), "r"(a1));
}

// ---------------------------------------------------------
// 3. HELPER: DYNAMIC INPUT PACKING (4-bit vs 8-bit)
// ---------------------------------------------------------
// Returns the physical number of bytes consumed by a single row
int generate_packed_inputs(volatile uint8_t *dst, int rows, int logical_len, int bit_width) {
    int physical_len = (bit_width == 4) ? (logical_len / 2) : logical_len;
    
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < physical_len; c++) {
            int idx = (r * physical_len) + c;
            
            if (bit_width == 4) {
                // Pack two logical values into one byte
                // Low nibble = 1, High nibble = 2 -> 0x21
                uint8_t val1 = 1; 
                uint8_t val2 = 1; 
                dst[idx] = (val2 << 4) | (val1 & 0x0F);
            } else {
                // 8-bit mode: Just standard integers
                dst[idx] = 1;
            }
        }
    }
    return physical_len; // e.g., returns 16 for 4-bit, 32 for 8-bit
}

void offline_weight_packing(volatile uint8_t *ordered_wgt, volatile uint8_t *logical_wgt) {
    int w_idx = 0;

    // Outer Loop: Hardware Tiles (Groups of 16 filters)
    for (int f_start = 0; f_start < NUM_FILTERS; f_start += HW_MAX_COLS) {
        
        int active_filters = NUM_FILTERS - f_start;
        if (active_filters > HW_MAX_COLS) active_filters = HW_MAX_COLS;

        // Inner Loop: Kernel Depth Chunks (Groups of 144 inputs)
        for (int k_start = 0; k_start < IN_LEN; k_start += HW_MAX_ROWS) {
            
            int active_len = IN_LEN - k_start;
            if (active_len > HW_MAX_ROWS) active_len = HW_MAX_ROWS;

            // PACKING: Grab the specific data needed for this hardware transaction
            for (int f = 0; f < active_filters; f++) {
                int real_f = f_start + f;
                if (PRECISION_W == 4) {
                    // 4-BIT MODE: Step by 2 and pack on the fly!
                    for (int k = 0; k < active_len; k += 2) {
                        uint8_t w1 = logical_wgt[(real_f * IN_LEN) + (k_start + k)];
                        uint8_t w2 = 0;
                        
                        // Safety check in case the active length is an odd number
                        if (k + 1 < active_len) {
                            w2 = logical_wgt[(real_f * IN_LEN) + (k_start + k + 1)];
                        }
                        
                        ordered_wgt[w_idx++] = (w2 << 4) | (w1 & 0x0F);
                    }
                } else {
                    // 8-BIT MODE: Standard 1-to-1 mapping
                    for (int k = 0; k < active_len; k++) {
                        ordered_wgt[w_idx++] = logical_wgt[(real_f * IN_LEN) + (k_start + k)];
                    }
            }
        }
    }
}
}


// ---------------------------------------------------------
// 4. MAIN DRIVER
// ---------------------------------------------------------
int main() {

    volatile uint8_t *input_img = (uint8_t*)(uintptr_t)(DMA_BASE);
    
    // We now have TWO weight pointers:
    // A. The original "Logical" weights (e.g., from a flat file)
    volatile uint8_t *raw_weights = (uint8_t*)(uintptr_t)(DMA_BASE + 0x8000);
    // B. The "Packed" weights (Optimized layout for hardware)
    volatile uint8_t *packed_weights = (uint8_t*)(uintptr_t)(DMA_BASE + 0x9000);
    
    // 3. Output Map (Size ~2KB) -> Offset 0x2000 (8KB)
    volatile int32_t *output_map = (int32_t*)(uintptr_t)(DMA_BASE + 0x10000);

    // =========================================================
    // B. INITIALIZE DATA
    // =========================================================
    // Fill Input: 1, 2, 3...
    int bytes_per_row = generate_packed_inputs(input_img, LOOP, IN_LEN, PRECISION_IN);
    
    // Fill Weights: All 1s
    for(int i=0; i<NUM_FILTERS*IN_LEN; i++) raw_weights[i] = 1; 
    offline_weight_packing(packed_weights, raw_weights);
    
    // Clear Outputs
    for(int i=0; i<NUM_FILTERS; i++) output_map[i] = 0;

    m5_reset_stats(0, 0);

    // =========================================================
    // C. CNN TILING LOOP
    // =========================================================

    // Loop 1: Filter Tiling (16 filters at a time)
    int packed_wgt_offset = 0;
    for (int f_start = 0; f_start < NUM_FILTERS; f_start += HW_MAX_COLS) {
        
        int active_filters = NUM_FILTERS - f_start;
        if (active_filters > HW_MAX_COLS) active_filters = HW_MAX_COLS;
        
        // Loop 2: Channel/Kernel Depth Tiling (144 inputs at a time)
        for (int k_start = 0; k_start < IN_LEN; k_start += HW_MAX_ROWS) {
            
            int active_len = IN_LEN - k_start;
            if (active_len > HW_MAX_ROWS) active_len = HW_MAX_ROWS;

            // --- LOAD WEIGHTS (PACKED) ---
            uint64_t current_tile_addr = (uint64_t)(uintptr_t)(packed_weights + packed_wgt_offset);

            // 2. Point DMA directly to DRAM. No CPU copy loop!
            reg_write(REG_WGT, current_tile_addr);
            
            // Calculate size for this tile (filters * inputs)
            int physical_weight_len = (PRECISION_W == 4) ? (active_len / 2) : active_len;
            int weight_tile_size = active_filters * physical_weight_len; 
            reg_write(REG_SIZE, weight_tile_size);
            reg_write(REG_ACTIVE_COLS, active_filters);
            reg_write(REG_BANK, 0);
            
            reg_write(REG_CMD, 1); // Trigger Weight DMA
            while(reg_read(REG_STATUS) == 1);

            // Advance offset for the next loop iteration
            packed_wgt_offset += weight_tile_size;

            // --- COMPUTE (SLIDING WINDOW) ---
            for (int j = 0; j < LOOP; j++) {

                // 2. Run Accelerator
                reg_write(REG_SRC, (uint64_t)(uintptr_t)(input_img+j*bytes_per_row));
                reg_write(REG_DST, (uint64_t)(uintptr_t)(output_map+j*NUM_FILTERS));
                reg_write(REG_SIZE, bytes_per_row); 
                reg_write(REG_ACTIVE_COLS, active_filters);
                reg_write(REG_ACTIVE_ROWS, active_len);
                
                reg_write(REG_CMD, 2); 
                while(reg_read(REG_STATUS) == 1); 

        }
    } 
}

// =========================================================
    // VERIFICATION
    // =========================================================
    // The expected math changes based on whether the data was packed!
    // int expected_val;
    // if (PRECISION_IN == 4) {
    //     // 16 bytes. Each byte holds '1' and '2'. Weights are '1'.
    //     // Math: 16 * ((1*1) + (2*1)) = 48
    //     expected_val = IN_LEN;
    // } else {
    //     // 32 bytes. Each byte is '1'. Weights are '1'.
    //     // Math: 32 * (1*1) = 32
    //     expected_val = IN_LEN * 1;
    // }

    // int errors = 0;
    // for (int r = 0; r < LOOP; r++) {
    //     for (int c = 0; c < NUM_FILTERS; c++) {
    //         int output_idx = (r * NUM_FILTERS) + c;
    //         int32_t actual_val = output_map[output_idx];
    //         printf("   Actual value %d ", actual_val);
            
    //         if (actual_val != expected_val) {
    //             if (errors < 5) {
    //                 printf("   [Error] Row %d, Filter %d -> Expected: %d, Got: %d\n", r, c, expected_val, actual_val);
    //             }
    //             errors++;
    //         }
    //     }
    // }

    // if (errors == 0) {
    //     printf("--- SUCCESS: All outputs are correct! ---\n");
    // } else {
    //     printf("--- FAILED: Found %d incorrect elements. ---\n", errors);
    // }

    m5_dump_stats(0, 0);

    return 0;
}