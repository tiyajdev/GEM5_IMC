#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// ------------------------------------------------------------------
// 1. HARDWARE DEFINITIONS (Organized)
// ------------------------------------------------------------------
#define IMC_BASE    0x30000000 // Base Address of Accelerator

// Register Offsets
#define REG_SRC     0x00       // Source Address (Input Data)
#define REG_DST     0x08       // Destination Address (Output Data)
#define REG_WGT     0x10       // Weight Address
#define REG_SIZE    0x18       // Transfer Size (Bytes)
#define REG_CMD     0x20       // Command Register (1=Load, 2=Compute)
#define REG_STATUS  0x28       // Status Register (1=Busy, 0=Idle)
#define REG_BANK    0x30       // Weight Bank ID (0 or 1)

// ------------------------------------------------------------------
// 2. MEMORY MAP DEFINITIONS (The Fix)
// ------------------------------------------------------------------
#define DMA_BASE    0x10000000 // Shared Data Region (Must match Python map!)

// Helper: Write to Register
void reg_write(int offset, uint64_t val) {
    *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset) = val;
}

// Helper: Read from Register
uint64_t reg_read(int offset) {
    return *(volatile uint64_t*)(uintptr_t)(IMC_BASE + offset);
}

int main() {
    printf("--- Accelerator Test: Organized & Mapped ---\n");

    int input_len = 144;
    int filters   = 16;
    
    // --------------------------------------------------------------
    // 3. POINTER SETUP
    // --------------------------------------------------------------
    // Instead of malloc, point directly to our known safe region.
    // We separate them by enough space (e.g., 4KB = 0x1000) to avoid overlap.
    
    volatile uint8_t *inputs  = (uint8_t *)(uintptr_t)(DMA_BASE);
    volatile uint8_t *weights = (uint8_t *)(uintptr_t)(DMA_BASE + 0x1000);
    volatile int32_t *outputs = (int32_t *)(uintptr_t)(DMA_BASE + 0x2000);

    printf(" > Inputs  @ 0x%lx\n", (uintptr_t)inputs);
    printf(" > Weights @ 0x%lx\n", (uintptr_t)weights);
    printf(" > Outputs @ 0x%lx\n", (uintptr_t)outputs);

    // --------------------------------------------------------------
    // 4. DATA INITIALIZATION
    // --------------------------------------------------------------
    // CPU writes to Virtual 0x40000000.
    // Because of .map(), this goes to Physical 0x40000000.
    for(int i=0; i<input_len; i++) {
        inputs[i] = 1;
    }
    for(int i=0; i<input_len * filters; i++) {
        weights[i] = 1; 
    }

    // --------------------------------------------------------------
    // 5. PHASE 1: LOAD WEIGHTS
    // --------------------------------------------------------------
    printf(" > CMD: Load Weights\n");
    reg_write(REG_WGT, (uint64_t)(uintptr_t)weights); // Use the SAFE address
    reg_write(REG_SIZE, input_len * filters);
    reg_write(REG_BANK, 0);
    reg_write(REG_CMD, 1); // 1 = Load Weights
    
    // Poll for completion
    while(reg_read(REG_STATUS) == 1); 

    // --------------------------------------------------------------
    // 6. PHASE 2: COMPUTE
    // --------------------------------------------------------------
    printf(" > CMD: Compute Vector\n");
    reg_write(REG_SRC, (uint64_t)(uintptr_t)inputs);
    reg_write(REG_DST, (uint64_t)(uintptr_t)outputs);
    reg_write(REG_SIZE, input_len);
    reg_write(REG_BANK, 0);
    reg_write(REG_CMD, 2); // 2 = Compute
    
    // Poll for completion
    while(reg_read(REG_STATUS) == 1); 

    printf("--- Accelerator: Complete ---\n");
    
    // --------------------------------------------------------------
    // 7. VERIFICATION
    // --------------------------------------------------------------
    // Expected: 144 inputs * 1 * 1 = 144
    int result = outputs[0];
    printf(" > Verification: Output[0] = %d ", result);
    
    if (result == 144) {
        printf("(SUCCESS)\n");
    } else {
        printf("(FAILED - Expected 144)\n");
    }

    return 0;
}