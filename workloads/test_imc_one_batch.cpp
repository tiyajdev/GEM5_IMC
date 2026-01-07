#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <cassert>
#include <sys/ioctl.h>

// --- HARDWARE CONFIGURATION ---
#define NUM_ROWS 32
#define NUM_COLS 6
#define PREC_INPUT 8
#define PREC_WEIGHT 4

#define REG_CMD 0x00
#define REG_STATUS 0x04
#define REG_PREC_W 0x08
#define REG_PREC_I 0x0C
#define REG_W_ADDR 0x10
#define REG_W_LEN 0x18
#define REG_I_ADDR 0x20
#define REG_I_LEN 0x28
#define REG_OUT_ADDR 0x30

#define CMD_COMPUTE 1
#define CMD_LOAD_W 2
#define CMD_LOAD_I 3
#define CMD_STORE_O 4

#define IOCTL_GET_PHYS_ADDR 0xCAFE

// --- THE SHARED STRUCT ---
// This must match the struct in imc_driver.cc
struct imc_ioctl_args
{
    uint64_t va_addr; // Input
    uint64_t pa_addr; // Output
};

volatile uint32_t *imc_regs = nullptr;
int driver_fd = -1;

// --- UPDATED HELPER: Uses Struct Pointer ---
uint64_t get_phys_addr(void *virtual_ptr)
{
    imc_ioctl_args args;
    args.va_addr = (uint64_t)virtual_ptr;
    args.pa_addr = 0; // Driver will fill this

    // Pass the ADDRESS of the struct (&args) to the driver
    if (ioctl(driver_fd, IOCTL_GET_PHYS_ADDR, &args) < 0)
    {
        printf("ERROR: Driver failed to translate VA %p\n", virtual_ptr);
        exit(1);
    }

    return args.pa_addr;
}

void write_reg(int offset, uint32_t value) { imc_regs[offset / 4] = value; }
uint32_t read_reg(int offset) { return imc_regs[offset / 4]; }
void wait_for_idle()
{
    while (read_reg(REG_STATUS) != 0)
    {
    }
}

int main()
{
    printf("--- STARTING IMC TEST (ROBUST 64-BIT MODE) ---\n");

    // 1. Open Driver
    driver_fd = open("/dev/imc", O_RDWR);
    if (driver_fd < 0)
    {
        perror("Failed to open /dev/imc");
        return 1;
    }

    // 2. Map Registers
    imc_regs = (uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, driver_fd, 0);
    if (imc_regs == MAP_FAILED)
    {
        perror("Failed to mmap");
        return 1;
    }

    // 3. Prepare Data
    std::vector<uint8_t> host_inputs(NUM_ROWS);
    for (int i = 0; i < NUM_ROWS; i++)
        host_inputs[i] = (i % 16) + 1;

    std::vector<uint8_t> host_weights_unpacked(NUM_ROWS * NUM_COLS);
    std::vector<uint8_t> host_weights_packed((NUM_ROWS * NUM_COLS * 4 + 7) / 8);

    // Random Weights
    for (int i = 0; i < NUM_ROWS * NUM_COLS; i++)
        host_weights_unpacked[i] = rand() % 16;

    // Pack Weights
    memset(host_weights_packed.data(), 0, host_weights_packed.size());
    for (int i = 0; i < NUM_ROWS * NUM_COLS; i++)
    {
        uint8_t val = host_weights_unpacked[i] & 0xF;
        int byte_idx = (i * 4) / 8;
        int bit_shift = (i * 4) % 8;
        host_weights_packed[byte_idx] |= (val << bit_shift);
    }

    std::vector<uint32_t> host_results(NUM_COLS, 0);

    // --- TRANSLATE ADDRESSES (Using new Struct method) ---
    uint64_t phys_inputs = get_phys_addr(host_inputs.data());
    uint64_t phys_weights = get_phys_addr(host_weights_packed.data());
    uint64_t phys_results = get_phys_addr(host_results.data());

    printf("Translations (64-bit safe):\n");
    printf("  Inputs VA: %p -> PA: 0x%lx\n", host_inputs.data(), phys_inputs);
    printf("  Weights VA: %p -> PA: 0x%lx\n", host_weights_packed.data(), phys_weights);
    printf("  Results VA: %p -> PA: 0x%lx\n", host_results.data(), phys_results);

    // 4. Run Hardware
    write_reg(REG_PREC_I, PREC_INPUT);
    write_reg(REG_PREC_W, PREC_WEIGHT);

    printf("DMA: Sending Inputs...\n");
    write_reg(REG_I_ADDR, phys_inputs);
    write_reg(REG_I_LEN, host_inputs.size());
    write_reg(REG_CMD, CMD_LOAD_I);
    wait_for_idle();

    printf("DMA: Sending Packed Weights...\n");
    write_reg(REG_W_ADDR, phys_weights);
    write_reg(REG_W_LEN, host_weights_packed.size());
    write_reg(REG_CMD, CMD_LOAD_W);
    wait_for_idle();

    printf("Compute: Triggering...\n");
    write_reg(REG_CMD, CMD_COMPUTE);
    wait_for_idle();

    printf("DMA: Reading Results...\n");
    write_reg(REG_OUT_ADDR, phys_results);
    write_reg(REG_CMD, CMD_STORE_O);
    wait_for_idle();

    // 5. Verify
    printf("--- VERIFICATION ---\n");
    int errors = 0;
    for (int c = 0; c < NUM_COLS; c++)
    {
        uint64_t expected = 0;
        for (int r = 0; r < NUM_ROWS; r++)
        {
            uint8_t input = host_inputs[r];
            uint8_t weight = host_weights_unpacked[r * NUM_COLS + c];
            expected += (uint64_t)input * weight;
        }

        uint32_t actual = host_results[c];
        printf("Col %d: Expected %lu | Actual %u", c, expected, actual);
        if (expected != actual)
        {
            printf(" [FAIL]\n");
            errors++;
        }
        else
        {
            printf(" [PASS]\n");
        }
    }

    if (errors == 0)
        printf("\n*** TEST PASSED! ***\n");
    else
        printf("\n*** TEST FAILED ***\n");

    close(driver_fd);
    return 0;
}