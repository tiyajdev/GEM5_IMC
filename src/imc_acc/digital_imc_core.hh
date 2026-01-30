#ifndef __IMC_ACC_DIGITAL_IMC_CORE_HH__
#define __IMC_ACC_DIGITAL_IMC_CORE_HH__

#include <vector>
#include "params/DigitalIMCCore.hh"
#include "sim/clocked_object.hh"
#include "base/statistics.hh" // Required for the stats struct

namespace gem5
{
    /**
     * DigitalIMCCore: The computational heart of the IMC Accelerator.
     * This class is a purely functional model of the validated IMC logic.
     * It does not handle gem5 events or timing directly; it relies on
     * the parent (IMCAccelerator) to schedule execution.
     */

    class DigitalIMCCore : public ClockedObject
    {
    private:
        // --- 1. Profile Data (from your IMCProfile) --
        const int Di;
        const int Do_dim;
        const int M;
        const int numMacros;
        int numWeightBanks;

        const int Bi;
        const int Bw;
        const int Bcycles;
        const bool inputSigned;
        const bool weightSigned;

        const double energyPerMacFj;

        const uint32_t mask;

        // Internal registers to store these values
        int regActiveCols;
        int regActiveRows;
        int currentComputeBank; // 0 or 1

        // --- 2. Data Buffers ---
        std::vector<uint8_t> inputBuffer;
        std::vector<uint8_t> weightBuffer;
        std::vector<int32_t> outputBuffer;

        // --- 3. The Math Logic ---
        // This is the function containing your validated loops
        void performFunctionalCompute();
        /**
         * Helper: Interprets a 4-bit value.
         *  val The raw byte.
         * is_signed_mode How to interpret this specific value.
         */
        int32_t decode4Bit(uint8_t val, bool isSigned);

    protected:
        // --- 4. Statistics ---
        struct IMCStats : public statistics::Group
        {
            IMCStats(statistics::Group *parent);

            statistics::Scalar totalOps;
            statistics::Scalar totalEnergyFj;
            statistics::Scalar totalStalls;
            statistics::Scalar totalProcessingLatency;
        } stats;

    public:
        DigitalIMCCore(const DigitalIMCCoreParams &p);

        // --- The Parent Interface ---

        // 1. Write Inputs (Standard broadcast)
        void setInputs(const std::vector<uint8_t> &in);

        // 2. Write Weights (To a specific background bank)
        void setWeights(const std::vector<uint8_t> &in, int bankID);

        // 3. Configure Gating (The "Shape" of the computation)
        void setActiveDimensions(int activeCols, int activeRows);

        // 4. Select Active Bank (Switch buffers)
        void setComputeBank(int bankID);

        /**
         * execute: Triggers the computation.
         * The Wrapper calls this exactly when the timer expires.
         */
        void execute();

        /**
         * getLatencyTicks: Calculates the execution duration.
         * The Wrapper uses this to schedule the "Done" event.
         * Returns: The number of gem5 Ticks the operation takes.
         */
        Cycles getLatencyCycles() const;

        /**
         * getOutput: Retrieves the results.
         * Called by the Wrapper to write data back to the Scratchpad.
         */
        const std::vector<int32_t> &getOutput() const;

        size_t getInputDriverSize() const
        {
            return (size_t)Di * M; // Returns bytes (assuming 8-bit input storage)
        }

        // Helper for the Wrapper to know how much to write back
        size_t getAccumulatorSize() const
        {
            // Output buffer is int32_t, so we multiply by 4
            return (size_t)Do_dim * numMacros * sizeof(int32_t);
        }

        /**
         * Manually clears the accumulators.
         * Essential for Output Stationary flow. Call this only when starting
         * a NEW set of output pixels. Do NOT call this between input tiles.
         */
        void resetAccumulators()
        {
            std::fill(outputBuffer.begin(), outputBuffer.end(), 0);
        }

        void addLatency(Tick ticks)
        {
            stats.totalProcessingLatency += ticks;
        }

        // Updated Buffer Clear method
        void clearInputBuffer()
        {
            // Fills with actual zeros, keeping the memory allocated (fast)
            std::fill(inputBuffer.begin(), inputBuffer.end(), 0);
        }
    };

} // namespace gem5

#endif // __IMC_ACC_DIGITAL_IMC_CORE_HH__