#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// WORKLOAD: 144 Inputs x 16 Filters
#define INPUT_LEN 144
#define FILTERS   16

// CPU Kernel: Integer Quantized GEMM
// Matches the IMC precision exactly (8-bit in, 32-bit out)
void cpu_gemm_int8(volatile int8_t* inputs, volatile int8_t* weights, volatile int32_t* outputs) {
    
    // Loop over each Filter (Column)
    for (int j = 0; j < FILTERS; j++) {
        int32_t sum = 0; // 32-bit Accumulator (to prevent overflow)
        
        // Loop over Input Vector (Row)
        for (int k = 0; k < INPUT_LEN; k++) {
            
            // 1. Fetch 8-bit Data
            int8_t in_val = inputs[k];
            int8_t w_val  = weights[k * FILTERS + j]; 
            
            // 2. Multiply & Accumulate
            // CPU must sign-extend 8-bit to 32-bit before multiplying
            sum += (int32_t)in_val * (int32_t)w_val;
        }
        outputs[j] = sum;
    }
}

int main() {
    printf("--- CPU Baseline: 8-bit Integer GEMM ---\n");
    
    // 1. Allocate Memory
    volatile int8_t  *inputs  = (int8_t*)malloc(INPUT_LEN * sizeof(int8_t));
    volatile int8_t  *weights = (int8_t*)malloc(INPUT_LEN * FILTERS * sizeof(int8_t));
    volatile int32_t *outputs = (int32_t*)malloc(FILTERS * sizeof(int32_t));

    // 2. Initialize (Dummy Data - 1s and 2s)
    for(int i=0; i<INPUT_LEN; i++) inputs[i] = 1;
    for(int i=0; i<INPUT_LEN*FILTERS; i++) weights[i] = 2;

    // 3. MEASURE THIS PART
    printf(" > Status: Running Integer GEMM...\n");
    cpu_gemm_int8(inputs, weights, outputs);

    printf("--- CPU Baseline: Complete. Result[0]=%d ---\n", outputs[0]);
    
    free((void*)inputs); free((void*)weights); free((void*)outputs);
    return 0;
}