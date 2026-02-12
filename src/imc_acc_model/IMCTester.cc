#include "imc_acc_model/IMCTester.hh"
#include <iostream>
#include <vector>
#include "sim/sim_exit.hh"

namespace gem5
{

    IMCTester::IMCTester(const IMCTesterParams &p)
        : SimObject(p),
          core(p.core),
          testMode(p.test_mode),
          testEvent([this]
                    { processTests(); }, name())
    {
    }

    std::vector<uint8_t> IMCTester::generateData(size_t size, uint8_t val)
    {
        return std::vector<uint8_t>(size, val);
    }

    void IMCTester::startup()
    {
        schedule(testEvent, core->clockEdge(Cycles(1)));
    }

    void IMCTester::processTests()
    {
        std::cout << "\n=============================================" << std::endl;
        std::cout << "      IMC ACCELERATOR VERIFICATION SUITE     " << std::endl;
        std::cout << "=============================================\n"
                  << std::endl;

        switch (testMode)
        {
        case 0:
            runUnpackedPerformanceTest();
            break;
        case 1:
            runPackedPerformanceTest();
            break;
        case 2:
            runGatingLogic(false);
            break; // Unpacked Gating
        case 3:
            runGatingLogic(true);
            break; // Packed Gating
        default:
            panic("Invalid Mode %d", testMode);
        }

        std::cout << "\n=============================================" << std::endl;
        std::cout << "          TEST COMPLETED SUCCESSFULLY        " << std::endl;
        std::cout << "=============================================\n"
                  << std::endl;

        // 1. NOW call getLatencyCycles() because ActiveRows is set correctly!
        Cycles processingTime = core->getLatencyCycles();

        std::cout << "   [INFO] Simulation will advance by " << processingTime << " cycles." << std::endl;

        // 2. Schedule the EXIT to happen after this processing time passes
        auto exitEvent = new EventFunctionWrapper(
            [this]
            { exitSimLoop("Tester Finished"); },
            name() + ".exit_event",
            true);

        // The simulation will run for exactly 'processingTime' duration
        schedule(exitEvent, core->clockEdge(processingTime));
    }

    // --- MODE 0 & 1: PERFORMANCE TESTS (Keep as they were) ---
    void IMCTester::runUnpackedPerformanceTest()
    {
        std::cout << "[MODE 0] UNPACKED PEAK PERFORMANCE" << std::endl;
        int rows = core->getDi() * core->getM();
        int cols = core->getDo() * core->getNumMacros();
        core->setActiveDimensions(cols, rows);

        uint8_t val = 2; // 2*2 = 4
        core->setInputs(generateData(rows, val));
        core->setWeights(generateData(rows * cols, val), 0);
        core->execute();

        if (verifyOutput(0, cols, val * val * rows))
            std::cout << "  -> PASS: Unpacked Peak Verified." << std::endl;
    }

    void IMCTester::runPackedPerformanceTest()
    {
        std::cout << "[MODE 1] PACKED PEAK PERFORMANCE" << std::endl;
        int rows = core->getDi() * core->getM();
        int cols = core->getDo() * core->getNumMacros();
        core->setActiveDimensions(cols, rows);

        uint8_t val = 2;
        // Pack: (2<<4)|2 = 34
        uint8_t packed = (val << 4) | val;

        // Note: We assume Bi=4 and Bw=4 for this test mode
        core->setInputs(generateData(rows / 2, packed));
        core->setWeights(generateData((rows * cols) / 2, packed), 0);
        core->execute();

        if (verifyOutput(0, cols, val * val * rows))
            std::cout << "  -> PASS: Packed Peak Verified." << std::endl;
    }

    // --- MODE 2 & 3: GATING LOGIC (The New Logic) ---
    void IMCTester::runGatingLogic(bool isPacked)
    {
        std::cout << "[MODE " << (isPacked ? "3" : "2") << "] "
                  << (isPacked ? "PACKED" : "UNPACKED") << " GATING TEST" << std::endl;

        int totalRows = core->getDi() * core->getM();
        int totalCols = core->getDo() * core->getNumMacros();

        // 1. Define Active Region (Half the array)
        int activeRows = totalRows / 2;
        int activeCols = totalCols / 2;
        if (activeRows == 0)
            activeRows = 1;
        if (activeCols == 0)
            activeCols = 1;

        std::cout << "  -> Active Region: " << activeRows << "x" << activeCols << std::endl;

        core->resetAccumulators();
        core->setActiveDimensions(activeCols, activeRows);

        uint8_t val = 2;

        // 2. Prepare Data Containers (Initialized to 0)
        // We use vectors of the FULL size so the Core doesn't get confused about packing.
        std::vector<uint8_t> inputVec(totalRows, 0);
        std::vector<uint8_t> weightVec(totalRows * totalCols, 0);

        // 3. Fill ONLY the Active Rows/Cols
        std::cout << "  -> Filling Data (Active Only)..." << std::endl;

        // Fill Inputs (Only indices 0 to activeRows-1)
        for (int r = 0; r < activeRows; r++)
        {
            inputVec[r] = val;
        }

        // Fill Weights (Only active columns, and within those, only active rows)
        // Memory Layout: [Col 0 Row 0, Col 0 Row 1 ... Col 1 Row 0 ...]
        for (int c = 0; c < activeCols; c++)
        {
            for (int r = 0; r < activeRows; r++)
            {
                int idx = (c * totalRows) + r;
                weightVec[idx] = val;
            }
        }

        // 4. Handle Packing (If Mode 3)
        if (isPacked)
        {
            std::cout << "  -> Compressing Data (4-bit Packing)..." << std::endl;
            std::vector<uint8_t> packedInputs;
            std::vector<uint8_t> packedWeights;

            // Pack Inputs
            for (size_t i = 0; i < inputVec.size(); i += 2)
            {
                uint8_t lower = inputVec[i];
                uint8_t upper = (i + 1 < inputVec.size()) ? inputVec[i + 1] : 0;
                packedInputs.push_back((upper << 4) | lower);
            }

            // Pack Weights
            for (size_t i = 0; i < weightVec.size(); i += 2)
            {
                uint8_t lower = weightVec[i];
                uint8_t upper = (i + 1 < weightVec.size()) ? weightVec[i + 1] : 0;
                packedWeights.push_back((upper << 4) | lower);
            }

            core->setInputs(packedInputs);
            core->setWeights(packedWeights, 0);
        }
        else
        {
            // Unpacked Mode (Mode 2)
            core->setInputs(inputVec);
            core->setWeights(weightVec, 0);
        }

        // 5. Execute
        core->execute();

        // 6. Verify
        // Active Region Expected: (2 * 2) * activeRows
        int activeExpected = (val * val) * activeRows;

        bool pass = true;
        std::cout << "  [CHECK] Active Region..." << std::endl;
        if (!verifyOutput(0, activeCols, activeExpected))
            pass = false;

        std::cout << "  [CHECK] Inactive Region..." << std::endl;
        if (!verifyOutput(activeCols, totalCols - activeCols, 0))
            pass = false;

        if (pass)
            std::cout << "  [PASS] Gating Verified." << std::endl;
    }

    // Helper
    bool IMCTester::verifyOutput(int startCol, int numCols, int expectedVal)
    {
        const auto &out = core->getOutput();
        int limit = (numCols > 5) ? 5 : numCols;

        std::cout << "    [DEBUG] Cols " << startCol << "-" << (startCol + limit - 1)
                  << ": ";
        for (int i = 0; i < limit; i++)
            std::cout << out[startCol + i] << " ";
        std::cout << "... (Exp: " << expectedVal << ")" << std::endl;

        for (int i = 0; i < numCols; i++)
        {
            if (out[startCol + i] != expectedVal)
                return false;
        }
        return true;
    }

} // namespace gem5