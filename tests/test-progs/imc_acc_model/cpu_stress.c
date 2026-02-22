#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define INPUT_LEN 32
#define FILTERS   24
#ifndef BATCH_SIZE
#define BATCH_SIZE 64
#endif
#define LOOPS BATCH_SIZE

// CPU Kernel (Standard Matrix Multiplication)
void cpu_gemm(volatile uint8_t* inputs, volatile uint8_t* weights, volatile int32_t* outputs) {
    for (int j = 0; j < FILTERS; j++) {
        int32_t sum = 0;
        for (int k = 0; k < INPUT_LEN; k++) {
            // Cast to int32_t to ensure positive multiplication
            sum += (int32_t)inputs[k] * (int32_t)weights[k * FILTERS + j];
        }
        outputs[j] = sum;
    }
}

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

int main() {
    //printf("--- CPU Stress Test: 1000 Unique Batches ---\n");
    
    // 1. ALLOCATION
    // Inputs/Weights are small (reused/static) 
    uint8_t  *inputs  = (uint8_t*)malloc(INPUT_LEN * LOOPS);
    uint8_t  *weights = (uint8_t*)malloc(INPUT_LEN * FILTERS);

    // Outputs must hold results for ALL 1000 loops
    // 16 filters * 1000 loops * 4 bytes = 64KB
    int32_t *all_outputs = (int32_t*)malloc(FILTERS * LOOPS * sizeof(int32_t));

    if (!inputs || !weights || !all_outputs) {
        //printf("Malloc failed!\n");
        return -1;
    }

    // 2. INITIALIZATION
    // Initialize Weights (Fixed at 1)
    for(int i=0; i<INPUT_LEN*FILTERS; i++) weights[i] = 1;

    for(int k=0; k<INPUT_LEN* LOOPS; k++) inputs[k] = 1;

    m5_reset_stats(0, 0);

    // 3. COMPUTE LOOP
    //printf(" > Starting Compute Loop...\n");
    
    for(int i=0; i<LOOPS; i++) {

        volatile uint8_t *current_input_ptr = &inputs[i * INPUT_LEN];
        volatile int32_t *current_output_ptr = &all_outputs[i * FILTERS];

        // C. Run Kernel
        cpu_gemm(current_input_ptr, weights, current_output_ptr);
    }

    m5_dump_stats(0, 0);
    // 4. VERIFICATION
    // printf(" > Verifying Results...\n");
    // int errors = 0;

    // for(int i=0; i<LOOPS; i++) {
    //     // Calculate Expected: (InputVal * 1) * 144 elements
    //     int expected_val =  INPUT_LEN; 
        
    //     // Check the first filter of this batch
    //     int32_t actual_val = all_outputs[i * FILTERS];

    //     if (actual_val != expected_val) {
    //          printf("   [Error] Loop %d: Expected %d, Got %d\n", i, expected_val, actual_val);
    //          errors++;
    //          if (errors > 10) break;
    //     }
    // }

    // if (errors == 0) {
    //     printf("--- SUCCESS: All %d CPU loops verified correctly! ---\n", LOOPS);
    // } else {
    //     printf("--- FAILED: Found %d errors ---\n", errors);
    // }
    
    
    free(inputs); free(weights); free(all_outputs);
    return 0;
}