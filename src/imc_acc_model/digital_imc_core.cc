#include "imc_acc_model/digital_imc_core.hh"
#include <cmath>
#include "base/trace.hh"
#include "sim/core.hh"

namespace gem5
{

    DigitalIMCCore::DigitalIMCCore(const DigitalIMCCoreParams &p) : ClockedObject(p),
                                                                    // Initialize ALL const variables defined in .hh
                                                                    Di(p.di),
                                                                    Do_dim(p.do_dim),
                                                                    M(p.m_batches),
                                                                    numMacros(p.num_macros),
                                                                    numWeightBanks(p.num_weight_banks),
                                                                    Bi(p.bi), // Added
                                                                    Bw(p.bw), // Added
                                                                    Bcycles(p.bcycles),
                                                                    inputSigned(p.input_signed),
                                                                    weightSigned(p.weight_signed),
                                                                    energyPerMacFj(p.energy_per_mac_fj),
                                                                    mask(p.mask),
                                                                    stats(this) // Initialize stats group
    {
        // Resize output buffer based on your logic
        inputBuffer.resize(Di * M, 0);
        weightBuffer.resize(Di * M * Do_dim * numMacros * numWeightBanks, 0);
        outputBuffer.resize(Do_dim * numMacros, 0);
        // --- 2. Initialize Registers ---
        regActiveCols = numMacros * Do_dim; // Default to Max
        regActiveRows = Di * M;             // Default to Max
        currentComputeBank = 0;             // Default to Bank 0
    }

    DigitalIMCCore::IMCStats::IMCStats(statistics::Group *parent)
        : statistics::Group(parent),
          totalOps(this, "total_ops", "Total Operations Executed"),
          totalEnergyFj(this, "total_energy_fj", "Total Energy (fJ)"),
          totalProcessingLatency(this, "total_processing_latency", "Total execution time in Ticks")
    {
    }

    int32_t DigitalIMCCore::decodeValue(uint8_t val, int bits, bool isSigned)
    {
        // 1. Mask the value to the correct width (e.g., 0xF for 4-bit, 0xFF for 8-bit)
        int32_t mask = (1 << bits) - 1;
        val &= mask;

        if (!isSigned)
            return (int32_t)val;

        // 2. Check Sign Bit (The MSB of the defined width)
        // For 4-bit, sign bit is bit 3 (1000 = 8)
        // For 8-bit, sign bit is bit 7 (10000000 = 128)
        int32_t signBit = 1 << (bits - 1);

        if (val & signBit)
        {
            // Sign Extension: If 4-bit value is 1010 (10), we want 1111...1010 (-6)
            // We subtract 2^bits. e.g., 10 - 16 = -6.
            return (int32_t)val - (1 << bits);
        }

        return (int32_t)val;
    }
    void DigitalIMCCore::setComputeBank(int bankID)
    {
        if (bankID >= numWeightBanks)
        {
            panic("DigitalIMCCore: Requesting invalid bank ID %d (Max %d)", bankID, numWeightBanks);
        }
        currentComputeBank = bankID;
    }

    void DigitalIMCCore::setWeights(const std::vector<uint8_t> &in, int bankID, int activeCols)
    {
        if (bankID >= numWeightBanks)
            panic("Invalid Bank ID!");

        // 1. Calculate Physical Hardware Stride
        // HW_MAX_ROWS = Di * M (e.g., 144)
        size_t hwStride = Di * M;
        size_t bankOffset = bankID * (hwStride * Do_dim * numMacros);

        // 2. Safety Check
        if (activeCols <= 0 || in.empty())
            return;

        // 3. Determine if we are in 4-bit mode based on Config
        bool is4BitMode = (Bw == 4);

        if (is4BitMode)
        {
            // --- 4-BIT UNPACKING + STRIDING ---

            // Logic: Input is packed bytes. 1 Byte = 2 Weights.
            // We need to calculate how many bytes belong to ONE column.
            size_t bytesPerCol = in.size() / activeCols;

            for (int col = 0; col < activeCols; ++col)
            {
                // Destination: Where this column starts in the sparse array
                // e.g. Col 0 @ 0, Col 1 @ 144...
                size_t destIdx = bankOffset + (col * hwStride);

                // Source: Where this column starts in the packed input vector
                size_t srcIdx = col * bytesPerCol;

                // Inner Loop: Unpack one column
                for (size_t i = 0; i < bytesPerCol; ++i)
                {
                    uint8_t packed = in[srcIdx + i];

                    // Lower 4 bits -> Weight 2*i
                    weightBuffer[destIdx + (2 * i)] = packed & 0xF;

                    // Upper 4 bits -> Weight 2*i + 1
                    weightBuffer[destIdx + (2 * i) + 1] = (packed >> 4) & 0xF;
                }
            }
        }
        else
        {
            // --- 8-BIT COPY + STRIDING ---

            // Logic: 1 Byte = 1 Weight.
            size_t rowsPerCol = in.size() / activeCols;

            for (int col = 0; col < activeCols; ++col)
            {
                // Destination: Sparse Stride (e.g. 144)
                size_t destIdx = bankOffset + (col * hwStride);

                // Source: Contiguous (Packed)
                size_t srcIdx = col * rowsPerCol;

                // Copy the column
                for (size_t r = 0; r < rowsPerCol; ++r)
                {
                    weightBuffer[destIdx + r] = in[srcIdx + r];
                }
                // Note: Indices [destIdx + rowsPerCol ... destIdx + 143] remain unchanged (or old garbage).
                // This is safe because your compute loop gates based on 'rowsInBatch'.
            }
        }
    }

    void DigitalIMCCore::setInputs(const std::vector<uint8_t> &in)
    {
        // 1. Safety Clear: Wipe old data to prevent "ghost" signals
        clearInputBuffer();

        // Calculate how many Logical Values (pixels/activations) the hardware expects
        size_t logicalSize = Di * M;

        // 2. CHECK: Is the input PACKED?
        // Logic: We are in 4-bit mode (Bi=4) AND the received data size is half the logical size.
        // (e.g., We need 64 inputs, but we received 32 bytes -> It must be packed).
        bool isPacked = (Bi == 4) && (in.size() <= (logicalSize + 1) / 2);

        if (isPacked)
        {
            // --- UNPACKING LOOP ---
            for (size_t i = 0; i < in.size(); ++i)
            {
                uint8_t packed = in[i];

                // Nibble 0 (Lower 4 bits) -> Index 2*i
                if ((2 * i) < inputBuffer.size())
                {
                    inputBuffer[2 * i] = packed & 0xF;
                }

                // Nibble 1 (Upper 4 bits) -> Index 2*i + 1
                if ((2 * i + 1) < inputBuffer.size())
                {
                    inputBuffer[2 * i + 1] = (packed >> 4) & 0xF;
                }
            }
        }
        else
        {
            // --- STANDARD COPY (Already Unpacked / 8-bit Mode) ---
            if (in.size() > inputBuffer.size())
            {
                panic("Input size (%d) exceeds Core buffer capacity (%d)!",
                      in.size(), inputBuffer.size());
            }
            std::copy(in.begin(), in.end(), inputBuffer.begin());
        }
    }

    Cycles DigitalIMCCore::getLatencyCycles() const
    {
        // 1. Calculate how many serial batches are actually needed based on Active Rows
        // (If hardware has 64 rows (Di) and we use 64 rows, batches = 1).
        int batchesNeeded = (regActiveRows + Di - 1) / Di;

        // 2. Compute Latency
        double cycles_per_op = (double)Bi / (double)Bcycles;
        double total_cycles = batchesNeeded * cycles_per_op;

        return Cycles((uint64_t)std::ceil(total_cycles));
    }

    void DigitalIMCCore::execute()
    {
        performFunctionalCompute();

        // 2. Calculate how long it took
        Cycles lat = getLatencyCycles();

        // 3. Update the Total Latency Statistic
        // We convert Cycles -> Ticks because gem5 stats usually track time in Ticks
        stats.totalProcessingLatency += cyclesToTicks(lat);
    }

    const std::vector<int32_t> &DigitalIMCCore::getOutput() const
    {
        return outputBuffer;
    }

    void DigitalIMCCore::setActiveDimensions(int activeCols, int activeRows)
    {
        // Clamp to hardware limits to prevent segfaults
        int maxCols = numMacros * Do_dim;
        int maxRows = Di * M;

        if (activeCols > maxCols)
            activeCols = maxCols;
        if (activeRows > maxRows)
            activeRows = maxRows;

        // Set Registers
        regActiveCols = activeCols;
        regActiveRows = activeRows;
    }

    void
    DigitalIMCCore::performFunctionalCompute()
    {
        // --- THIS IS YOUR EXACT LOGIC PORTED ---
        if (inputBuffer.empty() || weightBuffer.empty())
            return;

        // ---------------------------------------------------------
        // 1. HARDWARE RESET (The Fix)
        // ---------------------------------------------------------
        // Simulates the "Clear Accumulators" signal at the start of a cycle.
        // We fill the buffer with 0s so loop N doesn't inherit data from loop N-1.
        std::fill(outputBuffer.begin(), outputBuffer.end(), 0);

        long long currentRunOps = 0;
        double currentRunEnergy = 0.0;

        size_t bankSize = (Di * M * Do_dim * numMacros);
        size_t bankoffset = currentComputeBank * bankSize;

        // 1. Read Control Registers
        int remainingGlobalCols = regActiveCols;
        int totalActiveRows = regActiveRows;

        // Calculate how many batches we need to cover these rows
        // e.g. If Parent says "32 rows", and Di=16, we need 2 batches.
        // 3. Calculate Batches needed
        int batchesNeeded = (totalActiveRows + Di - 1) / Di;
        if (batchesNeeded > M)
            batchesNeeded = M; // Safety Clamp

        // 1. Loop Macros
        for (int m = 0; m < numMacros; ++m)
        {
            // Macro Gating: If this macro is beyond the active columns?
            // Actually, usually the Parent handles this by sending data only to active macros.
            // But if you broadcast:
            // If we are at Macro 1 (Columns 32-63) but Parent said "20 Columns total",
            // then this whole macro is idle.

            // Horizontal Gating Logic
            int colsInThisMacro = remainingGlobalCols;
            if (colsInThisMacro > Do_dim)
                colsInThisMacro = Do_dim;

            // Skip idle macros
            if (colsInThisMacro <= 0)
            {
                remainingGlobalCols -= Do_dim;
                continue;
            }

            // 2. Loop Batches (Temporal)
            for (int batch = 0; batch < batchesNeeded; ++batch)
            {
                // Calculate Active Rows for THIS batch
                // e.g. Total 20 rows.
                // Batch 0: rows 0-15 (16 rows)
                // Batch 1: rows 16-19 (4 rows)
                // Vertical Gating Logic
                int rowsProcessed = batch * Di;
                int rowsRemaining = totalActiveRows - rowsProcessed;
                int rowsInBatch = (rowsRemaining > Di) ? Di : rowsRemaining;

                if (rowsInBatch <= 0)
                    continue;

                // --- FUNCTIONAL BLOCK ---
                for (int col = 0; col < colsInThisMacro; ++col)
                {
                    for (int row = 0; row < rowsInBatch; ++row)
                    {
                        int inputIdx = (batch * Di) + row;
                        // Adjusted weight indexing to match flat buffer
                        int weightIdx = ((m * Do_dim + col) * (Di * M)) + inputIdx;

                        // Safety check
                        if (inputIdx < inputBuffer.size() && weightIdx < weightBuffer.size())
                        {
                            uint8_t raw_a = inputBuffer[inputIdx];
                            uint8_t raw_b = weightBuffer[weightIdx + bankoffset];

                            // 1. Decode Input (Activations)
                            int32_t val_a = decodeValue(raw_a, Bi, inputSigned);
                            int32_t val_b = decodeValue(raw_b, Bw, weightSigned);
                            // 3. Multiply and Accumulate
                            //    C++ handles the "Signed * Unsigned" automatically
                            //    because both are now safe int32_t containers.
                            outputBuffer[(m * Do_dim) + col] += val_a * val_b;

                            currentRunOps++; // Count 1 MAC
                        }
                    }
                }
                // Energy is paid for every MAC performed in the array (active or not usually, depends on gating)
                // Your logic: totalEnergy += (p.Di * p.Do * p.energyPerMac);
                currentRunEnergy += (rowsInBatch * colsInThisMacro * energyPerMacFj);
                // Energy Calculation:
                // Only pay for the batches we actually ran.
            }
            // Prepare for next macro
            remainingGlobalCols -= colsInThisMacro;
        }

        stats.totalOps += currentRunOps;

        stats.totalEnergyFj += currentRunEnergy;
    }

} // namespace gem5
