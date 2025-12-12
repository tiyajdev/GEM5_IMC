#ifndef __IMC_IMC_UNIT_HH__
#define __IMC_IMC_UNIT_HH__

#include <vector>

#include "base/statistics.hh"
#include "dev/dma_device.hh"
#include "params/IMCMAC.hh"
#include "sim/eventq.hh"

namespace gem5
{

    class IMCMAC : public DmaDevice
    {
    private:
        // =========================================================================
        // 1. HARDWARE ARCHITECTURE (Static Constraints)
        // =========================================================================

        // --- Array Geometry (The IMC Unit) ---
        const unsigned num_physical_rows; // N inputs (Vertical)
        const unsigned num_physical_cols; // N filters (Horizontal)

        // --- Architecture Logic ---
        const unsigned rows_per_compute_cycle; // Multiplexing Factor

        // --- Capacity & Precision (User Config) ---
        // These define the physical limitations of the hardware wires.
        const unsigned max_input_precision;
        const unsigned max_weight_precision;
        const unsigned max_output_precision;
        const unsigned num_entries; // Buffer Depth

        // --- Simulation Containers ---
        // How many bytes do we use in C++ to store one hardware element?
        unsigned input_bytes_per_element;
        unsigned weight_bytes_per_element;
        unsigned output_bytes_per_element;

        // --- Timing Parameters ---
        const Tick latency_sram_read;
        const Tick latency_cell_logic;
        const Tick latency_adder_tree;
        const bool enable_pipelining;

        // --- PIO Interface ---
        const Addr pioAddr;
        const Addr pioSize;
        const Tick pioDelay;

        // =========================================================================
        // 2. RUNTIME REGISTERS (MMIO)
        // =========================================================================

        struct Registers
        {
            uint32_t status;
            uint32_t cmd;
            uint32_t input_len;
            uint32_t num_vectors;
            uint32_t prec_input;
            uint32_t prec_weight;
            uint64_t dma_addr;
            uint32_t dma_len;
            uint32_t dma_type;
            // NEW: Data Format Register
            // Bit 0: Input Signed (1=Yes, 0=No)
            // Bit 1: Weight Signed (1=Yes, 0=No)
            uint32_t data_type;
        } regs;

        // Register Offsets
        static const int REG_STATUS = 0x00;
        static const int REG_CMD = 0x04;
        static const int REG_INPUT_LEN = 0x08;
        static const int REG_NUM_VECTORS = 0x0C;
        static const int REG_PREC_IN = 0x10;
        static const int REG_PREC_W = 0x14;
        static const int REG_DMA_ADDR_L = 0x18;
        static const int REG_DMA_ADDR_H = 0x1C;
        static const int REG_DMA_LEN = 0x20;
        static const int REG_DMA_TYPE = 0x24;
        static const int REG_DATA_TYPE = 0x28;

        // =========================================================================
        // 3. STORAGE BUFFERS
        // =========================================================================

        // We use uint8_t vectors as the raw backing store.
        // We use helper functions to cast these bytes into the correct integers.
        std::vector<uint8_t> input_buffer;
        std::vector<uint8_t> weight_array;
        std::vector<uint8_t> output_buffer;

        // Helpers to read safe values from the containers
        int32_t readInput(unsigned index);
        int32_t readWeight(unsigned index);

        // =========================================================================
        // 4. LOGIC & EVENTS
        // =========================================================================

        EventFunctionWrapper dmaDoneEvent;
        void dmaComplete();
        void startDma();

        EventFunctionWrapper computeDoneEvent;
        void processComputeEvent();

        void performCompute();
        void calculateLatency();

        Tick current_txn_latency;

        // =========================================================================
        // 5. STATISTICS
        // =========================================================================
        struct IMCMACStats : public statistics::Group
        {
            IMCMACStats(statistics::Group *parent);
            statistics::Scalar numComputes;
            statistics::Scalar totalCycles;
            statistics::Scalar numSramFetches;
            statistics::Scalar numSramWrites;
            statistics::Scalar totalMACs;
            statistics::Scalar dmaBytesRead;
            statistics::Scalar dmaBytesWritten;
        } stats;

    public:
        typedef IMCMACParams Params;
        IMCMAC(const Params &p);

        AddrRangeList getAddrRanges() const override;
        Tick read(PacketPtr pkt) override;
        Tick write(PacketPtr pkt) override;
    };

} // namespace gem5

#endif // __IMC_IMC_UNIT_HH__