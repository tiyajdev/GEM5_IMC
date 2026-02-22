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
// 2. LAYER CONFIGURATION
// ---------------------------------------------------------
#ifndef IN_H
#define IN_H 32
#endif

#ifndef IN_W
#define IN_W 32
#endif

#ifndef IN_C
#define IN_C 16
#endif

#ifndef K_H
#define K_H 3
#endif

#ifndef K_W
#define K_W 3
#endif

#ifndef NUM_FILTERS
#define NUM_FILTERS 32
#endif

#ifndef STRIDE
#define STRIDE 2
#endif

#ifndef PADDING
#define PADDING 1
#endif

#define K_LEN (K_H * K_W * IN_C) 
#define OUT_H ((IN_H - K_H + (2 * PADDING)) / STRIDE + 1)
#define OUT_W ((IN_W - K_W + (2 * PADDING)) / STRIDE + 1)

// ---------------------------------------------------------
// OPTIMIZED IM2COL HELPER
// ---------------------------------------------------------
void extract_partial_patch(volatile uint8_t *hw_dst, volatile uint8_t *img, 
                           int row_origin, int col_origin, 
                           int k_start, int active_len) {
    
    int current_k = 0; // Tracks logical position in the flattened 3x3x32 kernel
    int buffer_idx = 0;

    // Flattened Loop Logic
    for (int kh = 0; kh < K_H; kh++) {
        for (int kw = 0; kw < K_W; kw++) {
            for (int c = 0; c < IN_C; c++) {
                
                // CHECK: Is this pixel inside the current "chunk" we need?
                if (current_k >= k_start && current_k < (k_start + active_len)) {
                    
                    int r = row_origin + kh;
                    int w = col_origin + kw;
                    
                    if (r < 0 || r >= IN_H || w < 0 || w >= IN_W) {
                        hw_dst[buffer_idx++] = 0; // Padding
                    } else {
                        // Optimization: Use pointer arithmetic for speed if possible
                        // But array indexing is safer for clarity
                        int img_idx = (r * IN_W * IN_C) + (w * IN_C) + c;
                        hw_dst[buffer_idx++] = img[img_idx];
                    }
                }
                
                current_k++;
                // Early exit optimization
                if (current_k >= k_start + active_len) return;
            }
        }
    }
}

void offline_weight_packing(volatile uint8_t *ordered_wgt, volatile uint8_t *logical_wgt) {
    int w_idx = 0;

    // Outer Loop: Hardware Tiles (Groups of 16 filters)
    for (int f_start = 0; f_start < NUM_FILTERS; f_start += HW_MAX_COLS) {
        
        int active_filters = NUM_FILTERS - f_start;
        if (active_filters > HW_MAX_COLS) active_filters = HW_MAX_COLS;

        // Inner Loop: Kernel Depth Chunks (Groups of 144 inputs)
        for (int k_start = 0; k_start < K_LEN; k_start += HW_MAX_ROWS) {
            
            int active_len = K_LEN - k_start;
            if (active_len > HW_MAX_ROWS) active_len = HW_MAX_ROWS;

            // PACKING: Grab the specific data needed for this hardware transaction
            for (int f = 0; f < active_filters; f++) {
                int real_f = f_start + f;
                for (int k = 0; k < active_len; k++) {
                    int real_k = k_start + k;
                    // Store it linearly in the new "Optimized" buffer
                    ordered_wgt[w_idx++] = logical_wgt[(real_f * K_LEN) + real_k];
                }
            }
        }
    }
}

// ---------------------------------------------------------
// 4. MAIN DRIVER
// ---------------------------------------------------------
int main() {
    //printf("--- Running CNN Layer (S=%d, P=%d) via Direct DMA ---\n", STRIDE, PADDING);
    //printf("Shape: In[%dx%dx%d] -> Out[%dx%dx%d]\n", IN_H, IN_W, IN_C, OUT_H, OUT_W, NUM_FILTERS);

// =========================================================
    // A. MEMORY MAPPING (Tight TinyML Footprint - 64KB Spacing)
    // =========================================================
    
    // 1. Input Image (Max need: ~16KB) -> Offset 0
    volatile uint8_t *input_img = (uint8_t*)(uintptr_t)(DMA_BASE);
    
    // 2A. Raw Weights (Max need: ~36KB) -> Offset 0x10000 (64KB)
    volatile uint8_t *raw_weights = (uint8_t*)(uintptr_t)(DMA_BASE + 0x10000);
    
    // 2B. Packed Weights (Max need: ~36KB) -> Offset 0x20000 (128KB)
    volatile uint8_t *packed_weights = (uint8_t*)(uintptr_t)(DMA_BASE + 0x20000);
    
    // 3. Output Map (Max need: 64KB) -> Offset 0x30000 (192KB)
    volatile int32_t *output_map = (int32_t*)(uintptr_t)(DMA_BASE + 0x30000);

    // 4. Hardware Buffers (Tiny Scratchpads) -> Offset 0x40000 (256KB)
    volatile uint8_t *hw_input_buf  = (uint8_t*)(uintptr_t)(DMA_BASE + 0x40000);
    volatile uint8_t *hw_weight_buf = (uint8_t*)(uintptr_t)(DMA_BASE + 0x40100); // Safe +256 byte step
    volatile int32_t *hw_output_buf = (int32_t*)(uintptr_t)(DMA_BASE + 0x41000); // Safe +3.8KB step

    // =========================================================
    // B. INITIALIZE DATA
    // =========================================================
    // Fill Input: 1, 2, 3...
    for(int i=0; i<IN_H*IN_W*IN_C; i++) input_img[i] =  3;
    
    // Fill Weights: All 1s
    for(int i=0; i<NUM_FILTERS*K_LEN; i++) raw_weights[i] = 2; 
    offline_weight_packing(packed_weights, raw_weights);
    
    // Clear Outputs
    for(int i=0; i<OUT_H*OUT_W*NUM_FILTERS; i++) output_map[i] = 0;

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
        for (int k_start = 0; k_start < K_LEN; k_start += HW_MAX_ROWS) {
            
            int active_len = K_LEN - k_start;
            if (active_len > HW_MAX_ROWS) active_len = HW_MAX_ROWS;

            // --- LOAD WEIGHTS (PACKED) ---
            uint64_t current_tile_addr = (uint64_t)(uintptr_t)(packed_weights + packed_wgt_offset);

            // 2. Point DMA directly to DRAM. No CPU copy loop!
            reg_write(REG_WGT, current_tile_addr);
            
            // Calculate size for this tile (filters * inputs)
            int tile_size = active_filters * active_len; 
            reg_write(REG_SIZE, tile_size);
            reg_write(REG_ACTIVE_COLS, active_filters);
            reg_write(REG_BANK, 0);
            
            reg_write(REG_CMD, 1); // Trigger Weight DMA
            while(reg_read(REG_STATUS) == 1);

            // Advance offset for the next loop iteration
            packed_wgt_offset += tile_size;

            // --- COMPUTE (SLIDING WINDOW) ---
            for (int out_y = 0; out_y < OUT_H; out_y++) {
            for (int out_x = 0; out_x < OUT_W; out_x++) {
                
                int row_origin = (out_y * STRIDE) - PADDING;
                int col_origin = (out_x * STRIDE) - PADDING;

                // 1. OPTIMIZATION: Extract ONLY what we need DIRECTLY to HW buffer
                // No intermediate stack buffer, no memcpy loop.
                extract_partial_patch(hw_input_buf, (uint8_t*)input_img, 
                                      row_origin, col_origin, 
                                      k_start, active_len);

                // 2. Run Accelerator
                reg_write(REG_SRC, (uint64_t)(uintptr_t)hw_input_buf);
                reg_write(REG_DST, (uint64_t)(uintptr_t)hw_output_buf);
                reg_write(REG_SIZE, active_len); 
                reg_write(REG_ACTIVE_COLS, active_filters);
                reg_write(REG_ACTIVE_ROWS, active_len);
                
                reg_write(REG_CMD, 2); 
                while(reg_read(REG_STATUS) == 1); 

                // 3. Accumulate Results
                // (Your accumulation code is fine for now)
                for (int f = 0; f < active_filters; f++) {
                    int real_f = f_start + f;
                    int output_idx = (out_y * OUT_W * NUM_FILTERS) + (out_x * NUM_FILTERS) + real_f;
                    output_map[output_idx] += hw_output_buf[f];
                }
            }
        }
    } 
}

    // =========================================================
    // D. VERIFICATION
    // =========================================================
    // printf("--- Layer Complete ---\n");
    // printf("Pixel (0,0) Outputs (Expected ~21):\n");
    
    // int correct_count = 0;
    // for(int f=0; f<NUM_FILTERS; f++) {
    //     int val = output_map[f];
    //     printf(" Filter %d: %d\n", f, val);
    //     if (val > 0) correct_count++;
    // }
    
    // if (correct_count == NUM_FILTERS) {
    //     printf("--- SUCCESS: Non-zero output detected! ---\n");
    // } else {
    //     printf("--- WARNING: Some outputs are zero. Check Debug Logs. ---\n");
    // }

    m5_dump_stats(0, 0);

    return 0;
}