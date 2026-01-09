#include <iostream>
#include <vector>
#include <algorithm>
#include "input_image.h"
#include "weights_l1.h"
#include "weights_l2.h"
#include "bias_l1.h"
#include "bias_l2.h"

// Standard Software Matrix-Vector Multiplication
void software_fc_layer(const int8_t *weight, const int8_t *bias, const int8_t *input,
                       std::vector<int32_t> &output, int in_dim, int out_dim)
{
    for (int c = 0; c < out_dim; c++)
    {
        int32_t sum = bias[c];
        for (int r = 0; r < in_dim; r++)
        {
            // This is what the IMC does in hardware
            sum += (int32_t)input[r] * (int32_t)weight[c * in_dim + r];
        }
        output[c] = sum;
    }
}

int main()
{
    printf("Starting Software-only MNIST Baseline...\n");

    // Layer 1: 784 -> 64
    std::vector<int32_t> l1_out(64);
    software_fc_layer(w_l1, b_l1, img_input, l1_out, 784, 64);

    // Activation & Clamp
    std::vector<int8_t> next_in(64);
    for (int i = 0; i < 64; i++)
        next_in[i] = (int8_t)std::clamp(l1_out[i], 0, 127);

    // Layer 2: 64 -> 10
    std::vector<int32_t> scores(10);
    software_fc_layer(w_l2, b_l2, next_in.data(), scores, 64, 10);

    // Result
    int digit = std::distance(scores.begin(), std::max_element(scores.begin(), scores.end()));
    printf("SW Prediction: %d\n", digit);

    return 0;
}