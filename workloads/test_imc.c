#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>

// Register Offsets
#define REG_CMD       0x00
#define REG_STATUS    0x04
#define REG_SRC_ADDR  0x10 // 64-bit Register
#define REG_LEN       0x18
#define CMD_DMA_LOAD  2

// The Secret Physical Address (Must match the Driver!)
#define PHY_DATA_ADDR 0x10000000

int main() {
    printf("[SW] Opening Device...\n");
    int fd = open("/dev/imc", O_RDWR);
    if (fd < 0) { printf("Error opening device\n"); return -1; }

    // 1. ASK THE WAITER: "Give me 2 pages (8192 bytes)"
    //    - Page 0 will be Registers
    //    - Page 1 will be our Data Buffer
    volatile uint8_t *base = (volatile uint8_t *)mmap(NULL, 8192, 
                                PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    if (base == MAP_FAILED) { return -1; }

    // 2. IDENTIFY THE SPOTS
    volatile uint32_t *regs = (volatile uint32_t *)base;           // Page 0
    volatile uint8_t *data_buffer = (volatile uint8_t *)(base + 4096); // Page 1

    // 3. PREPARE THE INGREDIENTS
    //    We write to "data_buffer". Because of the driver, 
    //    this magically appears at Physical 0x88000000.
    printf("[SW] Writing data to buffer...\n");
    for (int i = 0; i < 64; i++) {
        data_buffer[i] = (uint8_t)i+122; // 0, 1, 2, 3...
    }

    // 4. TELL THE KITCHEN
    //    "Go directly to Fridge 0x88000000 to get the ingredients."
    printf("[SW] Configuring Hardware...\n");
    
    // Helper macros for cleaner code
    #define REG32(off)  (*((volatile uint32_t *)(base + off)))
    #define REG64(off)  (*((volatile uint64_t *)(base + off)))

    REG32(REG_LEN) = 64;
    REG64(REG_SRC_ADDR) = PHY_DATA_ADDR; // <--- The Critical Link

    // 5. START COOKING
    REG32(REG_CMD) = CMD_DMA_LOAD;

    // 6. WAIT
    while(REG32(REG_STATUS) != 0);

    printf("[SW] Done!\n");
    
    //munmap((void*)base, 8192);
    //close(fd);
    return 0;
}