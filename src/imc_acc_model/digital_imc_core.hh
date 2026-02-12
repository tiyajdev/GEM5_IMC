#ifndef __IMC_ACC_MODEL_DIGITAL_IMC_CORE_HH__
#define __IMC_ACC_MODEL_DIGITAL_IMC_CORE_HH__

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
        const int Di;             // number of inputs to the IMC
        const int Do_dim;         // number of outputs to the IMC
        const int M;              // multiplexing factor, so the number of weights that share the same
                                  // logic for multiplication and adder tree so they need to be processed sequentially
        const int numMacros;      //
        const int numWeightBanks; // this allows supporting simultaneous MAC and write

        const int Bi;            // input bit precision
        const int Bw;            // weight bit precision
        const int Bcycles;       // number of input bits processed per cycle
        const bool inputSigned;  // if input signed
        const bool weightSigned; // is weights signed

        const double energyPerMacFj; // energy per MAC operation

        const uint32_t mask;

        // Internal registers to store these values
        int regActiveCols;      // number of columns filled in the IMC(number of outputs/filters)
        int regActiveRows;      // number of rows filled in the IMC(number of inputs)
        int currentComputeBank; // which bank of weighs is currently used

        // --- 2. Data Buffers ---
        std::vector<uint8_t> inputBuffer;
        std::vector<uint8_t> weightBuffer;
        std::vector<int32_t> outputBuffer;

        // --- 3. The Math Logic ---
        // This is the function containing your validated loops
        void performFunctionalCompute();

        // The idea of this helper is that this model will represent IMC models that support
        // 8-bit and 4-bit precision, because the smallest unit that we can access is 8 bits
        // we need this decoder so that when we received the packed data from memory we will
        // depack each 8 bits into two 4 bits and store them in the 8 bit variables used here
        // before starting the computation.
        // General decoder that handles any bit width (4, 8, etc.)
        int32_t decodeValue(uint8_t val, int bits, bool isSigned);

    protected:
        // --- 4. Statistics ---
        struct IMCStats : public statistics::Group
        {
            IMCStats(statistics::Group *parent);

            statistics::Scalar totalOps;
            statistics::Scalar totalEnergyFj;
            statistics::Scalar totalProcessingLatency;
        } stats;

    public:
        DigitalIMCCore(const DigitalIMCCoreParams &p);
        virtual ~DigitalIMCCore() = default;
        // --- The Parent Interface ---
        // In DigitalIMCCore.hh under public:

        // In digital_imc_core.hh
        int getBw() const { return Bw; }
        int getBi() const { return Bi; }
        int getDi() const { return Di; }
        int getDo() const { return Do_dim; }
        int getM() const { return M; }
        int getNumMacros() const { return numMacros; }

        // 1. Write Inputs (Standard broadcast)
        void setInputs(const std::vector<uint8_t> &in);

        // 2. Write Weights (To a specific background bank)
        void setWeights(const std::vector<uint8_t> &in, int bankID, int activeCols);

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

#endif // __IMC_ACC_MODEL_DIGITAL_IMC_CORE_HH__
