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
#define HW_MAX_COLS 6   // Number of Filters (Do * NumMacros)

// Register Offsets
#define REG_SRC         0x00
#define REG_DST         0x08
#define REG_WGT         0x10
#define REG_SIZE        0x18
#define REG_CMD         0x20
#define REG_STATUS      0x28
#define REG_BANK        0x30
#define REG_ACTIVE_COLS 0x38

// Register Helpers
void reg_write(int offset, uint64_t val) {
    *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset) = val;
}
uint64_t reg_read(int offset) {
    return *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset);
}

// ---------------------------------------------------------
// 2. LAYER CONFIGURATION
// ---------------------------------------------------------
#define IN_H 32
#define IN_W 32
#define IN_C 3
#define K_H  3
#define K_W  3
#define NUM_FILTERS 16
#define STRIDE  1
#define PADDING 1

#define K_LEN (K_H * K_W * IN_C) 
#define OUT_H ((IN_H - K_H + (2 * PADDING)) / STRIDE + 1)
#define OUT_W ((IN_W - K_W + (2 * PADDING)) / STRIDE + 1)

// ---------------------------------------------------------
// 3. IM2COL HELPER
// ---------------------------------------------------------
void extract_patch_with_padding(volatile uint8_t *buffer, volatile uint8_t *img, int row_origin, int col_origin) {
    int idx = 0;
    for (int kh = 0; kh < K_H; kh++) {
        for (int kw = 0; kw < K_W; kw++) {
            for (int c = 0; c < IN_C; c++) {
                int r = row_origin + kh;
                int w = col_origin + kw;
                
                if (r < 0 || r >= IN_H || w < 0 || w >= IN_W) {
                    buffer[idx++] = 0; // Padding
                } else {
                    int img_idx = (r * IN_W * IN_C) + (w * IN_C) + c;
                    buffer[idx++] = img[img_idx];
                }
            }
        }
    }
}

// ---------------------------------------------------------
// 4. MAIN DRIVER
// ---------------------------------------------------------
int main() {
    printf("--- Running CNN Layer (S=%d, P=%d) via Direct DMA ---\n", STRIDE, PADDING);
    printf("Shape: In[%dx%dx%d] -> Out[%dx%dx%d]\n", IN_H, IN_W, IN_C, OUT_H, OUT_W, NUM_FILTERS);

    // =========================================================
    // A. MEMORY MAPPING (No Malloc!)
    // =========================================================
    // We partition the 0x10000000 region manually to ensure no overlaps.
    
    // 1. Input Image (Size ~100 bytes) -> Offset 0
    volatile uint8_t *input_img = (uint8_t*)(uintptr_t)(DMA_BASE);
    
    // 2. Weights (Size ~600 bytes) -> Offset 0x1000 (4KB)
    volatile uint8_t *weights   = (uint8_t*)(uintptr_t)(DMA_BASE + 0x8000);
    
    // 3. Output Map (Size ~2KB) -> Offset 0x2000 (8KB)
    volatile int32_t *output_map = (int32_t*)(uintptr_t)(DMA_BASE + 0x10000);

    // 4. Hardware Buffers (Temporary Scratchpad) -> Offset 0x3000 (12KB)
    volatile uint8_t *hw_input_buf  = (uint8_t*)(uintptr_t)(DMA_BASE + 0x30000);
    volatile uint8_t *hw_weight_buf = (uint8_t*)(uintptr_t)(DMA_BASE + 0x30100);
    volatile int32_t *hw_output_buf = (int32_t*)(uintptr_t)(DMA_BASE + 0x31000);

    // =========================================================
    // B. INITIALIZE DATA
    // =========================================================
    // Fill Input: 1, 2, 3...
    for(int i=0; i<IN_H*IN_W*IN_C; i++) input_img[i] = (i % 5) + 1;
    
    // Fill Weights: All 1s
    for(int i=0; i<NUM_FILTERS*K_LEN; i++) weights[i] = 1; 
    
    // Clear Outputs
    for(int i=0; i<OUT_H*OUT_W*NUM_FILTERS; i++) output_map[i] = 0;

    // =========================================================
    // C. CNN TILING LOOP
    // =========================================================

    // Loop 1: Filter Tiling (16 filters at a time)
    for (int f_start = 0; f_start < NUM_FILTERS; f_start += HW_MAX_COLS) {
        
        int active_filters = NUM_FILTERS - f_start;
        if (active_filters > HW_MAX_COLS) active_filters = HW_MAX_COLS;
        
        // Loop 2: Channel/Kernel Depth Tiling (144 inputs at a time)
        for (int k_start = 0; k_start < K_LEN; k_start += HW_MAX_ROWS) {
            
            int active_len = K_LEN - k_start;
            if (active_len > HW_MAX_ROWS) active_len = HW_MAX_ROWS;

            // --- LOAD WEIGHTS (PACKED) ---
            int w_idx = 0;
            for (int f = 0; f < active_filters; f++) {
                int real_f = f_start + f;
                for (int k = 0; k < active_len; k++) {
                    int real_k = k_start + k;
                    // Pack sequentially into scratchpad
                    hw_weight_buf[w_idx++] = weights[(real_f * K_LEN) + real_k];
                }
            }

            // Trigger DMA Load
            reg_write(REG_WGT, (uint64_t)(uintptr_t)hw_weight_buf);
            reg_write(REG_SIZE, w_idx);             // Total Packed Bytes
            reg_write(REG_ACTIVE_COLS, active_filters); // Hardware Unpacking Key
            reg_write(REG_BANK, 0);
            reg_write(REG_CMD, 1); 
            while(reg_read(REG_STATUS) == 1); 

            // --- COMPUTE (SLIDING WINDOW) ---
            for (int out_y = 0; out_y < OUT_H; out_y++) {
                for (int out_x = 0; out_x < OUT_W; out_x++) {
                    
                    int row_origin = (out_y * STRIDE) - PADDING;
                    int col_origin = (out_x * STRIDE) - PADDING;

                    // 1. Extract Patch to Scratchpad
                    // Note: We use a temp buffer on stack for the full patch, 
                    // then copy the slice we need to the DMA-visible buffer.
                    uint8_t full_patch[K_LEN];
                    extract_patch_with_padding(full_patch, (uint8_t*)input_img, row_origin, col_origin);

                    // 2. Copy slice to DMA Buffer
                    for(int i=0; i<active_len; i++) {
                        hw_input_buf[i] = full_patch[k_start + i];
                    }

                    // 3. Run Accelerator
                    reg_write(REG_SRC, (uint64_t)(uintptr_t)hw_input_buf);
                    reg_write(REG_DST, (uint64_t)(uintptr_t)hw_output_buf);
                    
                    reg_write(REG_SIZE, active_len); 
                    reg_write(REG_ACTIVE_COLS, active_filters); // Maintain Gating
                    
                    reg_write(REG_CMD, 2); 
                    while(reg_read(REG_STATUS) == 1); 

                    // 4. Accumulate Results (CPU reads from DMA Buffer)
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
    printf("--- Layer Complete ---\n");
    printf("Pixel (0,0) Outputs (Expected ~21):\n");
    
    int correct_count = 0;
    for(int f=0; f<NUM_FILTERS; f++) {
        int val = output_map[f];
        printf(" Filter %d: %d\n", f, val);
        if (val > 0) correct_count++;
    }
    
    if (correct_count == NUM_FILTERS) {
        printf("--- SUCCESS: Non-zero output detected! ---\n");
    } else {
        printf("--- WARNING: Some outputs are zero. Check Debug Logs. ---\n");
    }

    return 0;
}