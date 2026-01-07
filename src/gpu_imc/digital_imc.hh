#ifndef __GPU_IMC_DIGITAL_IMC_HH__
#define __GPU_IMC_DIGITAL_IMC_HH__

#include <vector>
#include "dev/dma_device.hh"
#include "params/DigitalIMC.hh"

namespace gem5
{

    class DigitalIMC : public DmaDevice
    {
    public:
        // --- 1. Standard Gem5 Interface (Public) ---
        typedef DigitalIMCParams Params;

        // Constructor
        DigitalIMC(const Params &p);

        // API Overrides (Address Range, Read, Write)
        AddrRangeList getAddrRanges() const override;
        Tick read(PacketPtr pkt) override;
        Tick write(PacketPtr pkt) override;

    protected:
        // --- 2. Statistics (Protected) ---
        // We keep stats protected in case future versions inherit from this.

        /**
         * Thesis Energy Model: Statistics Counters
         * We count events here, then multiply by Energy/Op in Python later.
         */
        struct ImcStats : public statistics::Group
        {
            ImcStats(statistics::Group *parent);

            // 1. Computation Stats
            statistics::Scalar numMacOps;      // Total MAC operations performed
            statistics::Scalar numTotalBitOps; // Total Bit-Level operations (Weight bits * Input bits)

            // 2. Memory Stats (SRAM Access)
            statistics::Scalar numSramReads;  // Total reads from Internal SRAM
            statistics::Scalar numSramWrites; // Total writes to Internal SRAM

            // 3. Data Movement
            statistics::Scalar totalDmaBytesRead;
            statistics::Scalar totalDmaBytesWritten;
        } stats;

    private:
        // --- 3. Internal Hardware State (Private) ---

        // --- MMIO Interface ---
        const Addr pioAddr;
        const Addr pioSize;
        const Tick pioDelay;

        // --- Control & Compute Registers (Offsets) ---
        static const int REG_CMD = 0x00;    // 1: Compute, 2: DMA Weight, 3: DMA Input
        static const int REG_STATUS = 0x04; // 0: Idle, 1: Busy
        static const int REG_PREC_W = 0x08; // Runtime Weight Precision
        static const int REG_PREC_I = 0x0C; // Runtime Input Precision

        // --- Configuration Registers ---
        static const int REG_W_ADDR = 0x10;   // Source Address for Weight DMA
        static const int REG_W_LEN = 0x18;    // Length for Weight DMA
        static const int REG_I_ADDR = 0x20;   // Source/Dest Address for Input/Output DMA
        static const int REG_I_LEN = 0x28;    // Length for Input DMA
        static const int REG_OUT_ADDR = 0x30; // Destination Offset in Output SRAM

        // --- Register State Storage ---
        struct Registers
        {
            uint32_t cmd;
            uint32_t status;
            uint32_t prec_w;
            uint32_t prec_i;

            uint64_t w_addr;
            uint32_t w_len;

            uint64_t i_addr;
            uint32_t i_len;
            uint64_t out_addr;
        } regs;

        // --- Fixed Hardware Geometry (From Python) ---
        int numRows;  // Number of Input Rows (e.g., 32)
        int numCols;  // Number of Output Columns (e.g., 6)
        int numBanks; // Parallel Banks
        int maxPrecW; // Maximum Hardware Weight Precision
        int maxPrecI; // Maximum Hardware Input Precision

        // --- Internal Storage (The "Workspace") ---
        // Physically fixed by geometry.
        std::vector<uint8_t> weight_memory;          // Stores Weights (Rows * Cols)
        std::vector<uint64_t> internal_accumulators; // Stores Partial Sums (Cols)

        // --- External Storage (The "Warehouse") ---
        // User-configurable Scratchpads (Input & Output Buffers).
        std::vector<uint8_t> input_sram;
        std::vector<uint8_t> output_sram;
        // Tracks where the next batch of results goes inside the Output SRAM
        uint64_t current_sram_offset;

        // --- Batching Logic (Internal Loop Control) ---
        uint32_t total_batches;     // How many batches to run?
        uint32_t current_batch_idx; // Which batch are we on?

        // --- 4. Internal Helpers & Handlers (Private) ---

        // Helper: Read Packed Bits
        // Extracts 'len' bits from a byte vector at arbitrary bit_offset.
        uint32_t readPackedBits(const std::vector<uint8_t> &buf, uint64_t bit_offset, int len);

        // Compute Engine (Events)
        EventFunctionWrapper computeDoneEvent;
        void startCompute();
        void computeDone();

        // DMA Engine (Events)
        EventFunctionWrapper dmaDoneEvent;
        void startDma();
        void dmaDone();
    };

} // namespace gem5

#endif // __GPU_IMC_DIGITAL_IMC_HH__