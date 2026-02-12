#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------
// 1. HARDWARE CONSTANTS
// ---------------------------------------------------------
#define IMC_BASE    0x30000000
#define HW_MAX_ROWS 144  // Input Length (Di * M)
#define HW_MAX_COLS 16   // Number of Filters (Do * NumMacros)

// Register Offsets
#define REG_SRC     0x00
#define REG_DST     0x08
#define REG_WGT     0x10
#define REG_SIZE    0x18
#define REG_CMD     0x20
#define REG_STATUS  0x28
#define REG_BANK    0x30

// Helpers
void reg_write(int offset, uint64_t val) {
    *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset) = val;
}
uint64_t reg_read(int offset) {
    return *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset);
}

// ---------------------------------------------------------
// 2. LAYER CONFIGURATION (ResNet-like Block)
// ---------------------------------------------------------
// Input: 7x7x2
#define IN_H 7
#define IN_W 7
#define IN_C 2

// Filter: 3x3, 32 Filters
#define K_H  3
#define K_W  3
#define NUM_FILTERS 32

// Hyperparameters
#define STRIDE  2
#define PADDING 1

// Flattened Input Vector Length (3*3*2 = 18)
#define K_LEN (K_H * K_W * IN_C) 

// Output Dimensions Formula: floor((W - K + 2P) / S) + 1
#define OUT_H ((IN_H - K_H + (2 * PADDING)) / STRIDE + 1)
#define OUT_W ((IN_W - K_W + (2 * PADDING)) / STRIDE + 1)

// ---------------------------------------------------------
// 3. IM2COL HELPER (With Padding Support)
// ---------------------------------------------------------
// Extracts a 3D patch and flattens it to 1D.
// If the requested pixel is outside the image boundary, returns 0.
void extract_patch_with_padding(uint8_t *buffer, const uint8_t *img, int row_origin, int col_origin) {
    int idx = 0;
    
    // Iterate over the Kernel Window
    for (int kh = 0; kh < K_H; kh++) {
        for (int kw = 0; kw < K_W; kw++) {
            for (int c = 0; c < IN_C; c++) {
                
                // Calculate actual pixel coordinate in Source Image
                int r = row_origin + kh;
                int w = col_origin + kw;

                // --- PADDING LOGIC ---
                if (r < 0 || r >= IN_H || w < 0 || w >= IN_W) {
                    // Out of bounds -> Zero Padding
                    buffer[idx++] = 0;
                } else {
                    // Valid Pixel -> Copy from Image
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
    printf("--- Running CNN Layer (S=%d, P=%d) ---\n", STRIDE, PADDING);
    printf("Shape: In[%dx%dx%d] -> Out[%dx%dx%d]\n", IN_H, IN_W, IN_C, OUT_H, OUT_W, NUM_FILTERS);

    // A. Memory Allocation
    uint8_t *input_img = (uint8_t*)malloc(IN_H * IN_W * IN_C);
    uint8_t *weights   = (uint8_t*)malloc(NUM_FILTERS * K_LEN);
    int32_t *output_map = (int32_t*)malloc(OUT_H * OUT_W * NUM_FILTERS * sizeof(int32_t));
    
    // Hardware Buffers (Tiling)
    uint8_t *hw_input_buf  = (uint8_t*)malloc(HW_MAX_ROWS);
    uint8_t *hw_weight_buf = (uint8_t*)malloc(HW_MAX_ROWS * HW_MAX_COLS);
    int32_t *hw_output_buf = (int32_t*)malloc(HW_MAX_COLS * sizeof(int32_t));

    // B. Initialize Data
    // Fill Input: Pattern 1, 2, 3...
    for(int i=0; i<IN_H*IN_W*IN_C; i++) input_img[i] = (i % 5) + 1;
    // Fill Weights: All 1s for easy verification
    for(int i=0; i<NUM_FILTERS*K_LEN; i++) weights[i] = 1; 
    // Clear Outputs
    memset(output_map, 0, OUT_H * OUT_W * NUM_FILTERS * sizeof(int32_t));

    // =========================================================
    // TILING LOOPS
    // =========================================================

    // Loop 1: Output Filters (e.g. 0..15, then 16..31)
    for (int f_start = 0; f_start < NUM_FILTERS; f_start += HW_MAX_COLS) {
        
        int active_filters = NUM_FILTERS - f_start;
        if (active_filters > HW_MAX_COLS) active_filters = HW_MAX_COLS;
        
        // Loop 2: Input Depth (Accumulation)
        for (int k_start = 0; k_start < K_LEN; k_start += HW_MAX_ROWS) {
            
            int active_len = K_LEN - k_start;
            if (active_len > HW_MAX_ROWS) active_len = HW_MAX_ROWS;

            // --- LOAD WEIGHTS FOR THIS TILE ---
            int w_idx = 0;
            for (int f = 0; f < active_filters; f++) {
                int real_f = f_start + f;
                for (int k = 0; k < active_len; k++) {
                    int real_k = k_start + k;
                    hw_weight_buf[w_idx++] = weights[(real_f * K_LEN) + real_k];
                }
            }
            reg_write(REG_WGT, (uint64_t)(uintptr_t)hw_weight_buf);
            reg_write(REG_SIZE, active_len * active_filters); 
            reg_write(REG_BANK, 0);
            reg_write(REG_CMD, 1); 
            while(reg_read(REG_STATUS) == 1); 

            // --- STREAM INPUTS (SLIDING WINDOW) ---
            // Loop 3 & 4: Spatial Locations (Output Map)
            for (int out_y = 0; out_y < OUT_H; out_y++) {
                for (int out_x = 0; out_x < OUT_W; out_x++) {
                    
                    // 1. CALCULATE ANCHOR (Top-Left of Receptive Field)
                    // The formula: input_coord = output_coord * stride - padding
                    int row_origin = (out_y * STRIDE) - PADDING;
                    int col_origin = (out_x * STRIDE) - PADDING;

                    // 2. EXTRACT PATCH (Im2Col + Padding)
                    uint8_t full_patch[K_LEN];
                    extract_patch_with_padding(full_patch, input_img, row_origin, col_origin);

                    // 3. TILE THE PATCH
                    memcpy(hw_input_buf, &full_patch[k_start], active_len);

                    // 4. RUN IMC
                    reg_write(REG_SRC, (uint64_t)(uintptr_t)hw_input_buf);
                    reg_write(REG_DST, (uint64_t)(uintptr_t)hw_output_buf);
                    reg_write(REG_SIZE, active_len); 
                    reg_write(REG_CMD, 2); 
                    while(reg_read(REG_STATUS) == 1); 

                    // 5. ACCUMULATE
                    for (int f = 0; f < active_filters; f++) {
                        int real_f = f_start + f;
                        int output_idx = (out_y * OUT_W * NUM_FILTERS) + (out_x * NUM_FILTERS) + real_f;
                        output_map[output_idx] += hw_output_buf[f];
                    }
                }
            }
        } // End K Loop
    } // End Filter Loop

    printf("--- Layer Complete ---\n");
    printf("Pixel (0,0) Outputs:\n");
    for(int f=0; f<NUM_FILTERS; f++) {
        printf(" Filter %d: %d\n", f, output_map[f]);
    }

    free(input_img); free(weights); free(output_map);
    free(hw_input_buf); free(hw_weight_buf); free(hw_output_buf);
    return 0;
}