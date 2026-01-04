/**
 * @file digital_imc.hh
 * @brief Header file for the DigitalIMC gem5 model.
 *
 * This device models a Digital In-Memory Computing (DIMC) unit based on the architecture
 * described in Sun et al., "Analog or Digital In-memory Computing Benchmarking through
 * Quantitative Modeling" (2023).
 *
 * It implements a bit-serial Matrix-Vector Multiplication (MVM) engine attached to the
 * system bus via PIO (control) and DMA (data movement).
 */

#ifndef __GPU_IMC_DIGITAL_IMC_HH__
#define __GPU_IMC_DIGITAL_IMC_HH__

#include <vector>            // Required for std::vector storage buffers
#include "dev/dma_device.hh" // Required for the base DmaDevice class
#include "params/DigitalIMC.hh"

namespace gem5
{

    class DigitalIMC : public DmaDevice
    {
    private:
        // --- PIO Interface (MMIO) ---
        // Address range and latency for CPU register accesses
        const Addr pioAddr;
        const Addr pioSize;
        const Tick pioDelay;

        // --- Registers Map (Offsets) ---
        // These offsets align with the software driver
        static const int REG_CMD = 0x00;         // 1=Compute, 2=LoadW, 3=LoadIn, 4=StoreOut
        static const int REG_STATUS = 0x04;      // 0=Idle, 1=Busy
        static const int REG_INPUT_PREC = 0x08;  // Configurable Input Precision (1-8 bits)
        static const int REG_WEIGHT_PREC = 0x0C; // Configurable Weight Precision (1-8 bits)

        // Dedicated DMA Registers
        // Weights (Set once per layer)
        static const int REG_WGT_ADDR = 0x10;
        static const int REG_WGT_LEN = 0x18;

        // Inputs (Set every batch)
        static const int REG_IN_ADDR = 0x20;
        static const int REG_IN_LEN = 0x28;

        // Outputs (Set every batch)
        static const int REG_OUT_ADDR = 0x30;
        // Note: Output Length is auto-calculated from BatchSize * NumCols

        static const int REG_BATCH = 0x38; // Number of vectors to process in this command

        // --- Register Storage Struct ---
        // Holds the actual values written by the CPU
        struct Registers
        {
            uint32_t cmd;
            uint32_t status;
            uint32_t inputPrec;
            uint32_t weightPrec;

            // DMA State Variables
            uint64_t wgtAddr;
            uint32_t wgtLen;
            uint64_t inAddr;
            uint32_t inLen;
            uint64_t outAddr;

            uint32_t batchSize;
        } regs;

        // --- Fixed Hardware Parameters (From Python Config) ---
        int numRows;       // Array Height (Parallelism Di)
        int numCols;       // Array Width (Parallelism Do)
        int maxWeightPrec; // Physical limit of weight cells (Bw)

        // --- Input/Output Buffer Geometry ---
        int maxInputPrec;    // Physical limit of input columns per input
        int maxInputsPerRow; // Physical limit of inputs packed in one row (Buffer Depth)

        // Calculated Parameters
        int inputRowSize;      // Bytes per Input Row (Derived from precision constraints)
        int outputBufferDepth; // Max number of output vectors the buffer can hold

        // --- Internal State & Buffers (The "Scratchpads") ---
        std::vector<uint8_t> weight_array;   // Stores Weights (Linearized 2D array)
        std::vector<uint8_t> input_buffer;   // 256KB-equivalent Input SRAM
        std::vector<uint64_t> output_buffer; // Output Accumulator/Register Buffer

        // --- Helpers ---
        // Helper to extract weight from linear array using (Row, Col)
        uint8_t getWeight(int row, int col) const
        {
            int index = (row * numCols) + col;
            if (index < 0 || index >= weight_array.size())
                return 0;
            return weight_array[index];
        }

        // --- Events (for Timing Simulation) ---
        EventFunctionWrapper computeDoneEvent;
        void startCompute();
        void computeDone();

        EventFunctionWrapper dmaDoneEvent;
        void startDma();
        void dmaDone();

    public:
        typedef DigitalIMCParams Params;

        // Constructor
        DigitalIMC(const Params &p);

        // --- Pure Virtual Functions (CPU Interaction) ---
        // Defines the memory range this device occupies
        AddrRangeList getAddrRanges() const override;

        // Handles CPU Reads to Registers
        Tick read(PacketPtr pkt) override;

        // Handles CPU Writes to Registers
        Tick write(PacketPtr pkt) override;
    };

} // namespace gem5

#endif // __GPU_IMC_DIGITAL_IMC_HH__