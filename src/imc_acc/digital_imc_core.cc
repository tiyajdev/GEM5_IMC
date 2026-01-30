#include "imc_acc/digital_imc_core.hh"
#include <cmath>
#include "base/trace.hh"
#include "debug/IMCAccelerator.hh"
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
                                                                    mask(p.mask),
                                                                    energyPerMacFj(p.energy_per_mac_fj),
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
          totalStalls(this, "Total cycles stalled waiting for memory"),
          totalProcessingLatency(this, "total_processing_latency", "Total execution time in Ticks")
    {
    }

    // --- Helper for 4-bit decoding ---
    int32_t DigitalIMCCore::decode4Bit(uint8_t val, bool isSigned)
    {
        val &= 0xF; // Mask to 4 bits
        if (!isSigned)
            return (int32_t)val;

        // Sign extension for 4-bit (Bit 3 is sign)
        if (val & 0x8)
            return (int32_t)val - 16;
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

    void DigitalIMCCore::setWeights(const std::vector<uint8_t> &in, int bankID)
    {
        if (bankID >= numWeightBanks)
            panic("Invalid Bank ID!");
        // 1. Calculate offset for this bank
        size_t bankSize = (Di * M * Do_dim * numMacros);
        size_t offset = bankID * bankSize;

        // 2. Write data to the specific bank area
        //    (Functional copy - happens "instantly" in C++, timing handled by Parent)
        if (in.size() > bankSize)
            panic("Weight packet too big for bank!");

        std::copy(in.begin(), in.end(), weightBuffer.begin() + offset);
    }

    void DigitalIMCCore::setInputs(const std::vector<uint8_t> &in)
    {
        // 1. Safety Clear: Wipe old data to prevent "ghost" signals
        clearInputBuffer();

        // Added Safety Check
        if (in.size() > inputBuffer.size())
        {
            panic("Input size (%d) exceeds Core buffer capacity (%d)!",
                  in.size(), inputBuffer.size());
        }
        // 3. Load new data (using std::copy is often safer/cleaner than assignment)
        std::copy(in.begin(), in.end(), inputBuffer.begin());
    }

    Cycles DigitalIMCCore::getLatencyCycles() const
    {

        // Avoid division by zero!
        if (Bcycles == 0)
            panic("Bcycles cannot be 0!");

        // 2. Calculate Cycles needed per Operation
        // Formula: We process 'Bi' bits, doing 'Bcycles' bits per cycle.
        // Example: 8 bits / 2 bits-per-cycle = 4 cycles.
        double cycles_per_op = (double)Bi / (double)Bcycles;

        // 3. Total Cycles for the whole job (M operations)
        double total_cycles = M * cycles_per_op;

        // 3. Convert to gem5 Cycles type
        // we use std::ceil because digital logic always snaps up to the next edge
        return Cycles((uint64_t)std::ceil(total_cycles));
    }

    void DigitalIMCCore::execute()
    {
        performFunctionalCompute();
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
                            //    ResNet usually: inputSigned = false (Unsigned 0..15)
                            int32_t val_a = decode4Bit(raw_a, inputSigned);

                            // 2. Decode Weight
                            //    ResNet usually: weightSigned = true (Signed -8..7)
                            int32_t val_b = decode4Bit(raw_b, weightSigned);
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