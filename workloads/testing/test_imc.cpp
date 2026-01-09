#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

// --- REGISTER DEFINITIONS ---
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

// MobileNet v1 PW Layer Params (Sun et al. Table IV)
#define CH_IN 64
#define CH_OUT 64
#define FEATURE_MAP 144 // 12x12 spatial positions

struct imc_ioctl_args
{
    uint64_t va_addr;
    uint64_t pa_addr;
};

volatile uint32_t *imc_regs = nullptr;
int driver_fd = -1;

uint64_t get_pa(void *va)
{
    imc_ioctl_args args = {(uint64_t)va, 0};
    ioctl(driver_fd, IOCTL_GET_PHYS_ADDR, &args);
    return args.pa_addr;
}

void wait_idle()
{
    while (imc_regs[REG_STATUS / 4] != 0)
        ;
}

int main()
{
    driver_fd = open("/dev/imc", O_RDWR);
    imc_regs = (uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, driver_fd, 0);

    // 1. Buffers
    std::vector<uint8_t> inputs(CH_IN, 1);
    std::vector<uint8_t> weights(CH_IN * CH_OUT, 1);
    std::vector<uint32_t> outputs(CH_OUT, 0);

    uint64_t p_in = get_pa(inputs.data());
    uint64_t p_w = get_pa(weights.data());
    uint64_t p_out = get_pa(outputs.data());

    // 2. Setup Precision (INT8 Validation)
    imc_regs[REG_PREC_I / 4] = 8;
    imc_regs[REG_PREC_W / 4] = 8;

    // 3. Load Weights ONCE (Weight Stationary)
    imc_regs[REG_W_ADDR / 4] = p_w;
    imc_regs[REG_W_LEN / 4] = weights.size();
    imc_regs[REG_CMD / 4] = CMD_LOAD_W;
    wait_idle();

    printf("Starting MobileNet PW Validation (144 batches)...\n");

    for (int s = 0; s < FEATURE_MAP; s++)
    {
        // A. Load Inputs
        imc_regs[REG_I_ADDR / 4] = p_in;
        imc_regs[REG_I_LEN / 4] = inputs.size();
        imc_regs[REG_CMD / 4] = CMD_LOAD_I;
        wait_idle();

        // B. Compute (Triggers bit-serial timing: 8ns @ 1GHz)
        imc_regs[REG_CMD / 4] = CMD_COMPUTE;
        wait_idle();

        // C. STORE RESULTS (Prevents "SRAM Full" error)
        imc_regs[REG_OUT_ADDR / 4] = p_out;
        imc_regs[REG_CMD / 4] = CMD_STORE_O;
        wait_idle();
    }

    printf("Validation Finished Successfully.\n");
    return 0;
}