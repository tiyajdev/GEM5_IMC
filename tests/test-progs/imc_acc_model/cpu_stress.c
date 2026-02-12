#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define INPUT_LEN 144
#define FILTERS   16
#define LOOPS     1000

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

int main() {
    printf("--- CPU Stress Test: 1000 Unique Batches ---\n");
    
    // 1. ALLOCATION
    // Inputs/Weights are small (reused/static) 
    uint8_t  *inputs  = (uint8_t*)malloc(INPUT_LEN);
    uint8_t  *weights = (uint8_t*)malloc(INPUT_LEN * FILTERS);

    // Outputs must hold results for ALL 1000 loops
    // 16 filters * 1000 loops * 4 bytes = 64KB
    int32_t *all_outputs = (int32_t*)malloc(FILTERS * LOOPS * sizeof(int32_t));

    if (!inputs || !weights || !all_outputs) {
        printf("Malloc failed!\n");
        return -1;
    }

    // 2. INITIALIZATION
    // Initialize Weights (Fixed at 1)
    for(int i=0; i<INPUT_LEN*FILTERS; i++) weights[i] = 1;

    // 3. COMPUTE LOOP
    printf(" > Starting Compute Loop...\n");
    
    for(int i=0; i<LOOPS; i++) {
        // A. Dynamic Input Generation (Same as IMC)
        // Cycle values 1..255 based on loop index
        uint8_t val = (uint8_t)((i % 255) + 1);
        for(int k=0; k<INPUT_LEN; k++) inputs[k] = val;

        // B. Calculate Output Pointer for THIS batch
        // We pass the address of the 16-int block for this specific loop
        volatile int32_t *current_output_ptr = &all_outputs[i * FILTERS];

        // C. Run Kernel
        cpu_gemm(inputs, weights, current_output_ptr);
    }

    // 4. VERIFICATION
    // printf(" > Verifying Results...\n");
    // int errors = 0;

    // for(int i=0; i<LOOPS; i++) {
    //     // Calculate Expected: (InputVal * 1) * 144 elements
    //     int expected_val = ((i % 255) + 1) * INPUT_LEN; 
        
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