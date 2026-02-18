#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------
// 1. HARDWARE ADDRESSES (Must match Python Script!)
// ---------------------------------------------------------
// Control Registers (Uncached in Python)
// We use uintptr_t to ensure it works on 64-bit RISC-V
#define ACC_CMD_ADDR  0x30000000
#define ACC_STAT_ADDR 0x30000004
#define SHARED_MEM_BASE 0x10000000

volatile uint32_t *ACC_REG_COMMAND = (uint32_t *)(uintptr_t)ACC_CMD_ADDR;
volatile uint32_t *ACC_REG_STATUS  = (uint32_t *)(uintptr_t)ACC_STAT_ADDR;

// ---------------------------------------------------------
// 2. LAYER CONFIGURATION
// ---------------------------------------------------------
#define IN_H 14
#define IN_W 14
#define IN_C 32
#define K_H  3
#define K_W  3
#define NUM_FILTERS 16
#define STRIDE  1
#define PADDING 1
#define K_LEN (K_H * K_W * IN_C)
#define OUT_H ((IN_H - K_H + (2 * PADDING)) / STRIDE + 1)
#define OUT_W ((IN_W - K_W + (2 * PADDING)) / STRIDE + 1)

int main() {
    // DISABLE BUFFERING: Forces printf to show up immediately
    setbuf(stdout, NULL);
    printf("--- IMC System Verification Test (64-bit Safe) ---\n");

    // =========================================================
    // A. SETUP POINTERS (64-BIT SAFE)
    // =========================================================
    
    // Calculate offsets
    uint64_t input_size = IN_H * IN_W * IN_C;
    uint64_t weight_size = NUM_FILTERS * K_LEN;
    uint64_t output_size = OUT_H * OUT_W * NUM_FILTERS * sizeof(int32_t);

    // Cast using (uintptr_t) to satisfy the 64-bit compiler
    volatile uint8_t *input_img = (uint8_t *)(uintptr_t)(SHARED_MEM_BASE);
    volatile uint8_t *weights   = (uint8_t *)(uintptr_t)(SHARED_MEM_BASE + input_size);
    volatile int32_t *output_map = (int32_t *)(uintptr_t)(SHARED_MEM_BASE + input_size + weight_size);

    // Use %p for printing pointers (handles 32 vs 64 bit automatically)
    printf("Mapped Memory:\n");
    printf(" Input  @ %p\n", (void *)input_img);
    printf(" Weights@ %p\n", (void *)weights);
    printf(" Output @ %p\n", (void *)output_map);

    // =========================================================
    // B. INITIALIZE DATA
    // =========================================================
    printf("Initializing Data...\n");
    
    // 1. Fill Input (1, 2, 3...)
    for(int i=0; i<input_size; i++) {
        input_img[i] = (i % 5) + 1;
    }
    
    // 2. Fill Weights (All 1s)
    for(int i=0; i<weight_size; i++) {
        weights[i] = 1;
    }
    
    // 3. Clear Output (Set to -1 to detect updates)
    for(int i=0; i<(output_size/4); i++) {
        output_map[i] = -1;
    }

    // =========================================================
    // C. RUN ACCELERATOR
    // =========================================================
    printf("--- Starting Accelerator ---\n");
    
    // 1. Send Start Command
    *ACC_REG_COMMAND = 1; 

    // 2. Poll Status Register (Wait for completion)
    int cycle_count = 0;
    while (*ACC_REG_STATUS == 0) {
        cycle_count++;
        // Busy Wait
    }
    printf("Accelerator Finished! (Polled %d times)\n", cycle_count);

    // =========================================================
    // D. VERIFY RESULTS
    // =========================================================
    printf("--- Results ---\n");
    int errors = 0;
    
    // Check the first 16 filters
    for(int f=0; f<16; f++) {
        int val = output_map[f];
        printf("Filter %02d: %d ", f, val);
        
        if (val == -1) {
            printf("[FAIL - Did not write]\n");
            errors++;
        } else if (val == 0) {
            printf("[WARN - Is zero correct?]\n");
        } else {
            printf("[OK]\n");
        }
    }

    if (errors == 0) {
        printf("\n*** TEST PASSED ***\n");
    } else {
        printf("\n*** TEST FAILED ***\n");
    }

    return 0;
}