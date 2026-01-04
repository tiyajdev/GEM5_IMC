/**
 * @file digital_imc.cc
 * @brief Implementation of the DigitalIMC gem5 model.
 * * Implements the logic for register access, DMA transfers, and bit-serial matrix multiplication.
 * Uses fixed-size SRAM buffers to model hardware constraints accurately.
 */

#include "gpu_imc/digital_imc.hh"

#include "base/trace.hh"
#include "debug/DigitalIMC.hh" // Debug flag for logging
#include "mem/packet_access.hh"
#include <cmath>   // Required for std::pow, std::log2, std::ceil used in reporting
#include <cstring> // Required for memset

namespace gem5
{

    DigitalIMC::DigitalIMC(const Params &p)
        : DmaDevice(p),
          pioAddr(p.pio_addr),
          pioSize(0x1000), // 4KB Address Space
          pioDelay(p.pio_latency),
          // Initialize Hardware Constraints (Fixed)
          numRows(p.num_rows),
          numCols(p.num_cols),
          maxWeightPrec(p.max_weight_prec),
          maxInputPrec(p.max_input_prec),
          maxInputsPerRow(p.max_inputs_per_row),
          // Initialize Events
          computeDoneEvent([this]
                           { computeDone(); }, name()),
          dmaDoneEvent([this]
                       { dmaDone(); }, name())
    {
        // ---------------------------------------------------------------------
        // 1. Calculate Input SRAM Capacity (Fixed Hardware Constraint)
        // ---------------------------------------------------------------------
        // Input capacity is defined by the Max Precision * Max Count.
        // We round up to nearest byte to determine the physical row width in bytes.
        int total_bits = maxInputPrec * maxInputsPerRow;
        inputRowSize = (total_bits + 7) / 8;

        // ---------------------------------------------------------------------
        // 2. Calculate Output Buffer Depth (Fixed Hardware Constraint)
        // ---------------------------------------------------------------------
        // The output buffer is physically sized to hold the maximum possible
        // number of input vectors (batches) that fit in the Input SRAM.
        outputBufferDepth = maxInputsPerRow;

        // ---------------------------------------------------------------------
        // 3. Allocate Fixed-Size Physical Memories ("Solder the Chips")
        // ---------------------------------------------------------------------
        // Weight Array: 1 Byte per Cell (for 2D indexing)
        weight_array.resize(numRows * numCols, 0);

        // Input SRAM: Fixed Bytes per Row * Num Rows
        input_buffer.resize(inputRowSize * numRows, 0);

        // Output Buffer: Fixed Depth * Num Cols
        // We use uint64_t to simulate the high-precision internal accumulator.
        output_buffer.resize(outputBufferDepth * numCols, 0);

        // ---------------------------------------------------------------------
        // 4. Calculate Accumulator Precision (Theoretical Reporting)
        // ---------------------------------------------------------------------
        // This calculates the theoretical max value to report required bit-width.
        // Formula: MaxVal = (2^In - 1) * (2^Wt - 1) * Rows
        double max_accumulator_val = (std::pow(2, maxInputPrec) - 1) * (std::pow(2, maxWeightPrec) - 1) * numRows;

        int bits_needed = 0;
        if (max_accumulator_val > 0)
        {
            bits_needed = (int)std::ceil(std::log2(max_accumulator_val));
        }

        // ---------------------------------------------------------------------
        // 5. Initialize Registers & Log
        // ---------------------------------------------------------------------
        // Zero out the register structure
        memset(&regs, 0, sizeof(regs));

        // Set defaults
        regs.inputPrec = maxInputPrec;
        regs.weightPrec = maxWeightPrec;
        regs.batchSize = 1;

        DPRINTF(DigitalIMC, "Created DigitalIMC: %dx%d Array.\n", numRows, numCols);
        DPRINTF(DigitalIMC, "  Hardware Init: Allocated Fixed SRAMs.\n");
        DPRINTF(DigitalIMC, "  Weights: %lu bytes\n", weight_array.size());
        DPRINTF(DigitalIMC, "  Inputs:  %lu bytes (%d bytes/row)\n", input_buffer.size(), inputRowSize);
        DPRINTF(DigitalIMC, "  Outputs: %lu entries (64-bit)\n", output_buffer.size());
        DPRINTF(DigitalIMC, "  Accumulator: Needs %d bits\n", bits_needed);
    }

    AddrRangeList
    DigitalIMC::getAddrRanges() const
    {
        return {RangeSize(pioAddr, pioSize)};
    }

    // -------------------------------------------------------------------------
    // CPU READ INTERFACE (MMIO)
    // -------------------------------------------------------------------------
    Tick
    DigitalIMC::read(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        unsigned size = pkt->getSize();
        uint64_t data = 0;

        switch (offset)
        {
        case REG_CMD:
            data = regs.cmd;
            break;
        case REG_STATUS:
            data = regs.status;
            break;
        case REG_INPUT_PREC:
            data = regs.inputPrec;
            break;
        case REG_WEIGHT_PREC:
            data = regs.weightPrec;
            break;

        // --- NEW REGISTER MAPPING ---
        case REG_WGT_ADDR:
            data = regs.wgtAddr;
            break;
        case REG_WGT_LEN:
            data = regs.wgtLen;
            break;
        case REG_IN_ADDR:
            data = regs.inAddr;
            break;
        case REG_IN_LEN:
            data = regs.inLen;
            break;
        case REG_OUT_ADDR:
            data = regs.outAddr;
            break;

        case REG_BATCH:
            data = regs.batchSize;
            break;
        default:
            data = 0;
            break;
        }

        if (size == 4)
            pkt->setLE<uint32_t>((uint32_t)data);
        else if (size == 8)
            pkt->setLE<uint64_t>(data);
        else
            panic("DigitalIMC: Read size %d not supported!\n", size);

        pkt->makeResponse();
        return pioDelay;
    }

    // -------------------------------------------------------------------------
    // CPU WRITE INTERFACE (MMIO)
    // -------------------------------------------------------------------------
    Tick
    DigitalIMC::write(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        unsigned size = pkt->getSize();

        // Handle OS Cleanup (munmap)
        // The OS might write 64-byte zeros to clear pages. We ignore this.
        if (size == 64)
        {
            pkt->makeResponse();
            return pioDelay;
        }

        uint64_t data = 0;
        if (size == 4)
            data = pkt->getLE<uint32_t>();
        else if (size == 8)
            data = pkt->getLE<uint64_t>();
        else
            panic("DigitalIMC: Write size %d not supported!\n", size);

        switch (offset)
        {
        case REG_CMD:
            regs.cmd = (uint32_t)data;
            // Decode Command
            if (regs.cmd == 1)
                startCompute();
            if (regs.cmd >= 2 && regs.cmd <= 4)
                startDma();
            break;

        case REG_INPUT_PREC:
            regs.inputPrec = (uint32_t)data;
            break;
        case REG_WEIGHT_PREC:
            regs.weightPrec = (uint32_t)data;
            break;

        // --- NEW REGISTER MAPPING ---
        // Weights
        case REG_WGT_ADDR:
            if (size == 8)
                regs.wgtAddr = data;
            else
                regs.wgtAddr = (regs.wgtAddr & 0xFFFFFFFF00000000) | (uint32_t)data;
            break;
        case REG_WGT_LEN:
            regs.wgtLen = (uint32_t)data;
            break;

        // Inputs
        case REG_IN_ADDR:
            if (size == 8)
                regs.inAddr = data;
            else
                regs.inAddr = (regs.inAddr & 0xFFFFFFFF00000000) | (uint32_t)data;
            break;
        case REG_IN_LEN:
            regs.inLen = (uint32_t)data;
            break;

        // Outputs
        case REG_OUT_ADDR:
            if (size == 8)
                regs.outAddr = data;
            else
                regs.outAddr = (regs.outAddr & 0xFFFFFFFF00000000) | (uint32_t)data;
            break;

        case REG_BATCH:
            regs.batchSize = (uint32_t)data;
            break;

        default:
            break;
        }

        pkt->makeResponse();
        return pioDelay;
    }

    // -------------------------------------------------------------------------
    // DMA LOGIC (Data Movement)
    // -------------------------------------------------------------------------
    void
    DigitalIMC::startDma()
    {
        if (regs.status != 0)
        {
            warn("DigitalIMC: Attempted to start DMA while busy!\n");
            return;
        }

        regs.status = 1; // Mark Device Busy
        uint8_t *host_buffer = nullptr;
        MemCmd cmd_type = MemCmd::ReadReq;
        Addr dma_addr = 0;
        int dma_len = 0;

        // Command 2: Load Weights (Uses WGT Registers)
        if (regs.cmd == 2)
        {
            // Hardware Bound Check: Panic if user tries to load more than physical capacity
            if (regs.wgtLen > weight_array.size())
            {
                panic("DMA Error: Weight Load Size (%d) exceeds Hardware Capacity (%lu)!\n",
                      regs.wgtLen, weight_array.size());
            }

            dma_addr = regs.wgtAddr;
            dma_len = regs.wgtLen;
            host_buffer = weight_array.data();
            cmd_type = MemCmd::ReadReq; // Reading FROM memory TO device

            DPRINTF(DigitalIMC, "DMA: Loading Weights from 0x%lx (%d bytes)\n", dma_addr, dma_len);
        }
        // Command 3: Load Inputs (Uses IN Registers)
        else if (regs.cmd == 3)
        {
            // Hardware Bound Check
            if (regs.inLen > input_buffer.size())
            {
                panic("DMA Error: Input Load Size (%d) exceeds Input SRAM Capacity (%lu)!\n",
                      regs.inLen, input_buffer.size());
            }

            dma_addr = regs.inAddr;
            dma_len = regs.inLen;
            host_buffer = input_buffer.data();
            cmd_type = MemCmd::ReadReq; // Reading FROM memory TO device

            DPRINTF(DigitalIMC, "DMA: Loading Inputs from 0x%lx (%d bytes)\n", dma_addr, dma_len);
        }
        // Command 4: Store Outputs (Uses OUT Register)
        else if (regs.cmd == 4)
        {
            // Automatic Length Calculation: We only store valid results.
            // Size = BatchSize * NumCols * 8 bytes (uint64_t)
            int valid_results = regs.batchSize * numCols;

            // Safety Check
            if (valid_results > output_buffer.size())
                valid_results = output_buffer.size();

            dma_len = valid_results * sizeof(uint64_t);
            dma_addr = regs.outAddr;
            host_buffer = (uint8_t *)output_buffer.data();
            cmd_type = MemCmd::WriteReq; // Writing FROM device TO memory

            DPRINTF(DigitalIMC, "DMA: Storing Results to 0x%lx (%d bytes)\n", dma_addr, dma_len);
        }

        // Trigger the DMA Transfer
        dmaPort.dmaAction(cmd_type, dma_addr, dma_len, &dmaDoneEvent, host_buffer, 0);
    }

    void
    DigitalIMC::dmaDone()
    {
        DPRINTF(DigitalIMC, "DMA Transaction Complete.\n");
        regs.status = 0; // Device Idle
        regs.cmd = 0;    // Clear Command
    }

    // -------------------------------------------------------------------------
    // COMPUTE LOGIC (The Bit-Serial Engine)
    // -------------------------------------------------------------------------
    void
    DigitalIMC::startCompute()
    {
        if (regs.status != 0)
        {
            warn("DigitalIMC: Compute busy!\n");
            return;
        }

        // 1. Validation Checks (Configuration vs Hardware Limits)
        if (regs.inputPrec > maxInputPrec)
            panic("Runtime Input Prec (%d) > HW Max (%d)!", regs.inputPrec, maxInputPrec);
        if (regs.weightPrec > maxWeightPrec)
            panic("Runtime Weight Prec (%d) > HW Max (%d)!", regs.weightPrec, maxWeightPrec);
        if (regs.batchSize > outputBufferDepth)
            panic("Batch Size (%d) exceeds Output Buffer Depth (%d)!", regs.batchSize, outputBufferDepth);

        regs.status = 1;
        DPRINTF(DigitalIMC, "CMD: Compute Start. Batch=%d, InPrec=%d, WgtPrec=%d\n",
                regs.batchSize, regs.inputPrec, regs.weightPrec);

        // 2. Clear Output Buffer (Simulate Reset/Accumulator Clear)
        std::fill(output_buffer.begin(), output_buffer.end(), 0);

        // 3. Perform Functional Computation
        // This models the cycle-accurate bit-serial behavior
        int bits_per_row = inputRowSize * 8;
        int inputs_processed = 0;

        // LOOP A: BATCH (Iterate through the logical inputs in the SRAM)
        // Stride is controlled by inputPrec.
        for (int batch_idx = 0; batch_idx < regs.batchSize; batch_idx++)
        {
            // Calculate Start Bit for this batch's input vector
            int col_start = batch_idx * regs.inputPrec;

            // Stop if we exceed physical SRAM width (Hardware Limit)
            if (col_start + regs.inputPrec > bits_per_row)
            {
                warn("Input Batch %d exceeds Fixed Input Row Width! Stopping.\n", batch_idx);
                break;
            }

            // LOOP B: BIT-SERIAL (Cycle-by-Cycle Processing)
            // DIMC processes 1 bit per cycle (B_cycle = 1 in paper)
            for (int b = 0; b < regs.inputPrec; b++)
            {
                int current_bit_col = col_start + b;

                // LOOP C: ROWS (Parallel Broadcast)
                for (int r = 0; r < numRows; r++)
                {
                    // --- Input Bit Extraction (Logic for Input Registers) ---
                    // "Transposes" the row-major storage to bit-serial feed
                    int byte_offset = current_bit_col / 8;
                    int bit_offset = current_bit_col % 8;
                    int global_idx = (r * inputRowSize) + byte_offset;

                    // Safety check
                    if (global_idx >= input_buffer.size())
                        continue;

                    bool input_bit = (input_buffer[global_idx] >> bit_offset) & 1;

                    if (!input_bit)
                        continue; // Optimization: Skip Zero Bits (Zero-Skipping)

                    // LOOP D: COLUMNS (Weights)
                    for (int c = 0; c < numCols; c++)
                    {
                        uint8_t w = getWeight(r, c);

                        // --- Weight Precision Masking ---
                        // Simulate lower precision hardware by masking unused bits
                        uint8_t mask = (1 << regs.weightPrec) - 1;
                        w = w & mask;

                        // Accumulate
                        // Output Index = (Batch * Cols) + Col
                        int out_idx = (batch_idx * numCols) + c;

                        // Safety Check for Output Buffer
                        if (out_idx < output_buffer.size())
                        {
                            // Weighted Sum: Add weight shifted by bit position
                            output_buffer[out_idx] += (w * (1 << b));
                        }
                    }
                }
            }
            inputs_processed++;
        }

        // 4. Calculate Latency & Schedule
        // Latency Formula: Inputs * Precision * Clock Period
        // This matches the linear scaling described in the paper.
        Tick latency = inputs_processed * regs.inputPrec * clockPeriod();

        schedule(computeDoneEvent, curTick() + latency);
    }

    void
    DigitalIMC::computeDone()
    {
        DPRINTF(DigitalIMC, "Compute Done.\n");

        // Print Debug info for the first result to verify math
        if (!output_buffer.empty() && regs.batchSize > 0)
        {
            DPRINTF(DigitalIMC, "Result[Batch0][Col0] = %lu\n", output_buffer[0]);
        }

        regs.status = 0; // Device Idle
        regs.cmd = 0;    // Clear Command
    }

} // namespace gem5