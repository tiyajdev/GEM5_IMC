#ifndef __IMC_ACC_MODEL_IMC_TESTER_HH__
#define __IMC_ACC_MODEL_IMC_TESTER_HH__

#include "sim/sim_object.hh"
#include "params/IMCTester.hh"
#include "imc_acc_model/digital_imc_core.hh"
#include "sim/eventq.hh" // Required for EventFunctionWrapper

namespace gem5
{

    class IMCTester : public SimObject
    {
    private:
        // Pointer to the IMC Core we are testing
        DigitalIMCCore *core;

        // Configuration from Python (0=Unpacked Peak, 1=Packed Peak, 2=Unpacked Gating, 3=Packed Gating)
        int testMode;

        // Event Wrapper to schedule tests inside the simulation loop
        EventFunctionWrapper testEvent;

        // Main entry point for testing (called by the event)
        void processTests();

        // --- Helper Functions ---
        // Generates a vector of specific size filled with a value
        std::vector<uint8_t> generateData(size_t size, uint8_t val);

        // Verifies a range of output columns against an expected value
        // Returns true if all columns match, false otherwise
        bool verifyOutput(int startCol, int numCols, int32_t expectedVal);

        // --- Test Logic ---
        // Mode 0: Full Array, 8-bit/Unpacked style
        void runUnpackedPerformanceTest();

        // Mode 1: Full Array, 4-bit/Packed style (checks bandwidth logic)
        void runPackedPerformanceTest();

        // Mode 2 & 3: Partial Array (Active Region only)
        // isPacked = false -> Mode 2 (Standard Gating)
        // isPacked = true  -> Mode 3 (Compressed Gating)
        void runGatingLogic(bool isPacked);

    public:
        // Constructor & Startup
        IMCTester(const IMCTesterParams &p);
        void startup() override;
    };

} // namespace gem5

#endif // __IMC_ACC_MODEL_IMC_TESTER_HH__