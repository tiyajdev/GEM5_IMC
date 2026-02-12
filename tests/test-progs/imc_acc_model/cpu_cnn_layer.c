#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------
// 1. LAYER CONFIGURATION (Must match IMC exactly)
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

// Flattened Kernel Length (for Weight Indexing)
#define K_LEN (K_H * K_W * IN_C)

// Output Dimensions
#define OUT_H ((IN_H - K_H + (2 * PADDING)) / STRIDE + 1)
#define OUT_W ((IN_W - K_W + (2 * PADDING)) / STRIDE + 1)

// ---------------------------------------------------------
// 2. MAIN DRIVER
// ---------------------------------------------------------
int main() {
    printf("--- CPU Baseline CNN Layer (S=%d, P=%d) ---\n", STRIDE, PADDING);
    printf("Shape: In[%dx%dx%d] -> Out[%dx%dx%d]\n", IN_H, IN_W, IN_C, OUT_H, OUT_W, NUM_FILTERS);

    // A. Memory Allocation
    uint8_t *input_img = (uint8_t*)malloc(IN_H * IN_W * IN_C);
    uint8_t *weights   = (uint8_t*)malloc(NUM_FILTERS * K_LEN);
    int32_t *output_map = (int32_t*)malloc(OUT_H * OUT_W * NUM_FILTERS * sizeof(int32_t));
    
    if (!input_img || !weights || !output_map) {
        printf("Malloc failed!\n");
        return -1;
    }

    // B. Initialize Data (Exact Match to IMC)
    // Fill Input: Pattern 1, 2, 3...
    for(int i=0; i<IN_H*IN_W*IN_C; i++) input_img[i] = (i % 5) + 1;
    // Fill Weights: All 1s
    for(int i=0; i<NUM_FILTERS*K_LEN; i++) weights[i] = 1; 
    // Clear Outputs
    memset(output_map, 0, OUT_H * OUT_W * NUM_FILTERS * sizeof(int32_t));

    // =========================================================
    // C. CPU CONVOLUTION (Standard 6-Loop Implementation)
    // =========================================================
    
    // 1. Loop over Output Rows (Height)
    for (int out_y = 0; out_y < OUT_H; out_y++) {
        
        // 2. Loop over Output Cols (Width)
        for (int out_x = 0; out_x < OUT_W; out_x++) {
            
            // Calculate Anchor Point in Input (Top-Left)
            int row_origin = (out_y * STRIDE) - PADDING;
            int col_origin = (out_x * STRIDE) - PADDING;

            // 3. Loop over Filters (Depth)
            for (int f = 0; f < NUM_FILTERS; f++) {
                
                int32_t sum = 0; // Accumulator for this pixel

                // --- CONVOLUTION WINDOW ---
                // 4. Loop Kernel Rows
                for (int ky = 0; ky < K_H; ky++) {
                    // 5. Loop Kernel Cols
                    for (int kx = 0; kx < K_W; kx++) {
                        // 6. Loop Input Channels
                        for (int c = 0; c < IN_C; c++) {
                            
                            // A. Calculate Input Coordinates
                            int in_row = row_origin + ky;
                            int in_col = col_origin + kx;

                            // B. Padding Check (Boundary Test)
                            uint8_t pixel_val = 0;
                            if (in_row >= 0 && in_row < IN_H && 
                                in_col >= 0 && in_col < IN_W) {
                                // Valid Pixel: Fetch from Input Array
                                int in_idx = (in_row * IN_W * IN_C) + (in_col * IN_C) + c;
                                pixel_val = input_img[in_idx];
                            }
                            // else: pixel_val remains 0 (Zero Padding)

                            // C. Fetch Weight
                            // Weight Layout: [Filter][Row][Col][Channel]
                            // This matches the order of loops in IMC's extract_patch
                            int w_idx = (f * K_LEN) + 
                                        (ky * K_W * IN_C) + 
                                        (kx * IN_C) + c;
                            
                            uint8_t weight_val = weights[w_idx];

                            // D. MAC Operation
                            sum += (int32_t)pixel_val * (int32_t)weight_val;
                        }
                    }
                } // End Window Loops

                // Store Result
                int out_idx = (out_y * OUT_W * NUM_FILTERS) + (out_x * NUM_FILTERS) + f;
                output_map[out_idx] = sum;

            } // End Filter Loop
        } // End Width Loop
    } // End Height Loop

    printf("--- CPU Layer Complete ---\n");
    printf("Pixel (0,0) Outputs:\n");
    for(int f=0; f<NUM_FILTERS; f++) {
        printf(" Filter %d: %d\n", f, output_map[f]);
    }

    free(input_img); free(weights); free(output_map);
    return 0;
}