#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// 1. HARDWARE DEFINITIONS
#define IMC_BASE    0x30000000
#define DMA_BASE    0x10000000 // Shared Memory at 256MB

#define REG_SRC     0x00
#define REG_DST     0x08
#define REG_WGT     0x10
#define REG_SIZE    0x18
#define REG_CMD     0x20
#define REG_STATUS  0x28
#define REG_BANK    0x30

#define INPUT_LEN   144
#define FILTERS     16
#define LOOPS       1000  // Run 1000 independent batches

// Helper for Register Access
void reg_write(int offset, uint64_t val) {
    *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset) = val;
}

uint64_t reg_read(int offset) {
    return *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset);
}

int main() {
    printf("--- IMC Stress Test: 1000 Unique Batches ---\n");

    // ---------------------------------------------------------
    // 2. MEMORY SETUP
    // ---------------------------------------------------------
    // We need enough space for 1000 output batches (16 ints each)
    // 1000 * 16 * 4 bytes = ~64KB. This fits easily in the 256MB DRAM.
    
    volatile uint8_t *inputs  = (uint8_t *)(uintptr_t)(DMA_BASE);
    volatile uint8_t *weights = (uint8_t *)(uintptr_t)(DMA_BASE + 0x1000);
    
    // Outputs start at offset 0x4000 to be safe
    volatile int32_t *outputs = (int32_t *)(uintptr_t)(DMA_BASE + 0x4000);

    // Initialize Weights (Fixed at 1 for simplicity)
    for(int i=0; i<INPUT_LEN * FILTERS; i++) weights[i] = 1;

    // ---------------------------------------------------------
    // 3. PHASE 1: LOAD WEIGHTS (Once)
    // ---------------------------------------------------------
    printf(" > Loading Weights...\n");
    reg_write(REG_WGT, (uint64_t)(uintptr_t)weights);
    reg_write(REG_SIZE, INPUT_LEN * FILTERS);
    reg_write(REG_BANK, 0);
    reg_write(REG_CMD, 1); 
    while(reg_read(REG_STATUS) == 1); 

    // ---------------------------------------------------------
    // 4. PHASE 2: COMPUTE LOOP (1000 Times)
    // ---------------------------------------------------------
    printf(" > Starting Compute Loop...\n");
    
    for (int i = 0; i < LOOPS; i++) {
        // A. Dynamic Input Generation
        // For Loop 0, inputs are 1. For Loop 1, inputs are 2. Etc.
        // Since we re-use the same input buffer address, we overwrite it here.
        uint8_t val = (uint8_t)((i % 255) + 1); // Cycle 1..255
        for(int k=0; k<INPUT_LEN; k++) inputs[k] = val;

        // B. Calculate Output Address for THIS batch
        // We want outputs[0..15], then outputs[16..31], etc.
        uint64_t current_dst_addr = (uint64_t)(uintptr_t)(outputs + (i * FILTERS));

        // C. Trigger Accelerator
        reg_write(REG_SRC, (uint64_t)(uintptr_t)inputs);
        reg_write(REG_DST, current_dst_addr); // <--- NEW DESTINATION EVERY TIME
        reg_write(REG_SIZE, INPUT_LEN);
        reg_write(REG_BANK, 0);
        reg_write(REG_CMD, 2); 

        // D. Wait for Completion
        while(reg_read(REG_STATUS) == 1);
    }

    // ---------------------------------------------------------
    // 5. VERIFICATION
    // ---------------------------------------------------------
    printf(" > Verifying Results...\n");
    int errors = 0;
    
    for (int i = 0; i < LOOPS; i++) {
        int expected_val = ((i % 255) + 1) * INPUT_LEN; // val * 144 * 1
        
        // Check the first filter of this batch (since all filters are 1, they are identical)
        int32_t actual_val = outputs[i * FILTERS]; 
        
        if (actual_val != expected_val) {
            printf("   [Error] Loop %d: Expected %d, Got %d\n", i, expected_val, actual_val);
            errors++;
            if (errors > 10) { printf("   ... Stopping verification (too many errors)\n"); break; }
        }
    }

    if (errors == 0) {
        printf("--- SUCCESS: All %d loops verified correctly! ---\n", LOOPS);
    } else {
        printf("--- FAILED: Found %d errors ---\n", errors);
    }

    return 0;
}