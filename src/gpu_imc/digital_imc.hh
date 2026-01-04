#ifndef __GPU_IMC_DIGITAL_IMC_HH__
#define __GPU_IMC_DIGITAL_IMC_HH__

#include <vector>            //<--- CHANGED: Added vector include
#include "dev/dma_device.hh" // <--- CHANGED: Must include dma_device.hh
#include "params/DigitalIMC.hh"

namespace gem5
{

    class DigitalIMC : public DmaDevice
    {
    private:
        // --- PIO Interface (MMIO) ---
        const Addr pioAddr;
        const Addr pioSize;
        const Tick pioDelay;

        // --- Control & Compute Registers (0x00 - 0x0F) ---
        static const int REG_CMD = 0x00;    // 1: Start Compute, 2: Start DMA
        static const int REG_STATUS = 0x04; // Read 0=Idle, 1=Busy
        static const int REG_PREC = 0x08;   // Runtime Precision (1, 2, 4, 8 bits)

        // --- DMA Configuration Registers (0x10 - 0x1F) ---
        static const int REG_SRC_ADDR = 0x10; // DRAM Address (Pointer to Weights)
        static const int REG_LEN = 0x18;      // Length of data in Bytes to copy

        // --- Register Storage ---
        struct Registers
        {
            uint32_t cmd;
            uint32_t status;
            uint32_t prec;
            uint64_t srcAddr; // 64-bit Address!
            uint32_t len;
        } regs;

        // --- Fixed Hardware Parameters (From Python) ---
        int numRows;      // Di (e.g., 32 or 128)
        int numCols;      // Do (e.g., 6)
        int numBanks;     // Parallel banks
        int maxPrecision; // Physical constraint (e.g., 8-bit)

        // --- Internal State & DMA Buffers ---
        std::vector<uint8_t> weight_array; // Buffer to store weights loaded from DRAM

        // --- Events ---
        // 1. Event for when Compute finishes (Timing Model)
        EventFunctionWrapper computeDoneEvent;
        void startCompute();
        void computeDone();

        // 2. Event for when DMA finishes (Data Movement)
        EventFunctionWrapper dmaDoneEvent;
        void startDma();
        void dmaDone();

    public:
        typedef DigitalIMCParams Params;

        // Constructor
        DigitalIMC(const Params &p);

        // --- Pure Virtual Functions (CPU Interaction) ---
        AddrRangeList getAddrRanges() const override;
        Tick read(PacketPtr pkt) override;
        Tick write(PacketPtr pkt) override;
    };

} // namespace gem5

#endif // __GPU_IMC_DIGITAL_IMC_HH__