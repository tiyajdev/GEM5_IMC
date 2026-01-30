#include "imc_acc/imc_accelerator.hh"
#include "debug/IMCAccelerator.hh"
#include "sim/system.hh"

#include <algorithm> // For std::min
#include <cstring>   // For std::memcpy

namespace gem5
{

    // ===========================================================================
    //                            CONSTRUCTOR & SETUP
    // ===========================================================================

    IMCAccelerator::IMCAccelerator(const IMCAcceleratorParams &p) : ClockedObject(p),
                                                                    pioAddr(p.pio_addr), // [NEW] Read Address from Python
                                                                    pioSize(p.pio_size), // [NEW] Read Size from Python
                                                                                         // 1. Initialize blockedPacket FIRST (matches .hh order)
                                                                    blockedPacket(nullptr),

                                                                    //  2. Initialize Ports SECOND
                                                                    inputSpadPort(p.name + ".input_spad_port", this),
                                                                    outputSpadPort(p.name + ".output_spad_port", this),
                                                                    // 3. Command Port (MMIO)
                                                                    commandPort(p.name + ".pio_port", this),

                                                                    dmaPort(this, p.system),

                                                                    //  3. Initialize Components THIRD
                                                                    imcCore(p.imc_core),
                                                                    system(p.system),
                                                                    // requestorId is initialized in the body below

                                                                    // 6. Internal Registers (Reset Values)
                                                                    regWeightAddr(0),
                                                                    regInputAddr(0),
                                                                    regOutputAddr(0),
                                                                    regSize(0),

                                                                    // 7. Job Config Holders
                                                                    weightDramAddr(0), inputDramAddr(0), outputDramAddr(0),
                                                                    weightSize(0), inputSize(0),
                                                                    currentInputOffset(0), currentOutputOffset(0),
                                                                    //  4. Initialize Buffer FOURTH
                                                                    transitBuffer(nullptr),

                                                                    //  5. Initialize State FIFTH
                                                                    state(IDLE),

                                                                    //  6. Initialize Events SIXTH
                                                                    computeEvent([this]
                                                                                 { processComputeEvent(); }, name()),
                                                                    dmaDoneEvent([this]
                                                                                 { dmaDone(); }, name())
    {
        // Allocate the Transit Buffer (Internal FIFO / Staging Buffer)
        // 4KB is large enough to hold typical burst transactions.
        transitBuffer = new uint8_t[4096];

        // [FIXED] Register this device with the System to get a valid ID
        requestorId = system->getRequestorId(this);
    }

    IMCAccelerator::~IMCAccelerator()
    {
        if (transitBuffer)
            delete[] transitBuffer;
    }

    Port &IMCAccelerator::getPort(const std::string &if_name, PortID idx)
    {
        if (if_name == "input_spad_port")
            return inputSpadPort;
        if (if_name == "output_spad_port")
            return outputSpadPort;
        if (if_name == "dma_port")
            return dmaPort;
        if (if_name == "pio_port")
            return commandPort;
        return ClockedObject::getPort(if_name, idx);
    }

    void IMCAccelerator::startup()
    {
        // We wait for the Python script (Driver) to trigger us.
        DPRINTF(IMCAccelerator, "Accelerator initialized. Waiting for MMIO commands...\n");
    }

    void IMCAccelerator::init()
    {
        ClockedObject::init();

        // Notify the system that we are alive and have addresses to claim
        if (commandPort.isConnected())
        {
            commandPort.sendRangeChange();
        }
    }

    AddrRangeList IMCAccelerator::getAddrRanges() const
    {
        AddrRangeList ranges;
        // pioAddr and pioSize come from Python params
        ranges.push_back(RangeSize(pioAddr, pioSize));
        return ranges;
    }

    AddrRangeList IMCAccelerator::CommandPort::getAddrRanges() const
    {
        return owner->getAddrRanges();
    }

    Tick IMCAccelerator::CommandPort::recvAtomic(PacketPtr pkt)
    {
        return owner->handleRegisterAccess(pkt);
    }

    void IMCAccelerator::CommandPort::recvFunctional(PacketPtr pkt)
    {
        owner->handleRegisterAccess(pkt);
    }

    bool IMCAccelerator::CommandPort::recvTimingReq(PacketPtr pkt)
    {
        // 1. Safety Check: If we are already waiting to send a packet, retry later.
        if (pendingPacket != nullptr)
        {
            return false;
        }

        // 2. Process the Logic IMMEDIATELY (Read/Write registers)
        // This calculates the latency (e.g., 10ns) and prepares the packet as a Response.
        owner->handleRegisterAccess(pkt);
        // 3. Store the packet for later
        pendingPacket = pkt;

        // 4. Schedule the "Send" event for the future
        // This ensures the response happens AFTER the current clock cycle,
        // preventing the System XBar timing violation.
        owner->schedule(sendEvent, owner->clockEdge(Cycles(1)));

        return true;
    }

    //  Helper to send the packet when the event fires
    void IMCAccelerator::CommandPort::trySendPacket()
    {
        assert(pendingPacket != nullptr);

        // Try to send the response to the XBar
        bool success = sendTimingResp(pendingPacket);

        if (success)
        {
            // Success! The XBar accepted it. We are free.
            pendingPacket = nullptr;
        }
        else
        {
            // The XBar is busy. We do NOT clear pendingPacket.
            // We wait for recvRespRetry() to be called.
            DPRINTF(IMCAccelerator, "MMIO Response blocked. Waiting for XBar retry.\n");
        }
    }

    void IMCAccelerator::CommandPort::recvRespRetry()
    {
        // The XBar is ready now. Try sending again.
        if (pendingPacket != nullptr)
        {
            trySendPacket();
        }
    }

    Tick IMCAccelerator::handleRegisterAccess(PacketPtr pkt)
    {
        // 1. Calculate Register Offset (Mask to 4KB window)
        Addr offset = pkt->getAddr() & 0xFFF;

        DPRINTF(IMCAccelerator, "MMIO Access: Offset %#x\n", offset);

        if (pkt->isWrite())
        {
            // --- WRITE HANDLER ---
            uint64_t data = pkt->getLE<uint64_t>();

            switch (offset)
            {
            case 0x00:
                regWeightAddr = data;
                DPRINTF(IMCAccelerator, "MMIO: Set Weight Addr = %#x\n", regWeightAddr);
                break;
            case 0x08:
                regInputAddr = data;
                DPRINTF(IMCAccelerator, "MMIO: Set Input Addr = %#x\n", regInputAddr);
                break;
            case 0x10:
                regOutputAddr = data;
                DPRINTF(IMCAccelerator, "MMIO: Set Output Addr = %#x\n", regOutputAddr);
                break;
            case 0x18:
                regSize = (uint32_t)data;
                DPRINTF(IMCAccelerator, "MMIO: Set Size = %d\n", regSize);
                break;
            case 0x20: // COMMAND REGISTER (GO)
                if (data == 1)
                {
                    DPRINTF(IMCAccelerator, "MMIO: CMD 'GO' received!\n");
                    // Trigger the logic using the registers we just wrote
                    startProcessing(regWeightAddr, regSize, regInputAddr, regSize, regOutputAddr);
                }
                break;
            default:
                warn("MMIO: Unknown register write offset: %#x", offset);
            }
        }
        else if (pkt->isRead())
        {
            // --- READ HANDLER ---
            uint64_t data = 0;
            switch (offset)
            {
            case 0x00:
                data = regWeightAddr;
                break;
            case 0x08:
                data = regInputAddr;
                break;
            case 0x10:
                data = regOutputAddr;
                break;
            case 0x18:
                data = regSize;
                break;
            case 0x20:
                data = (state == IDLE) ? 0 : 1;
                break; // Status: 1=Busy, 0=Idle
            default:
                data = 0;
            }
            pkt->setLE<uint64_t>(data);
        }

        // Turn the request packet into a response packet
        pkt->makeResponse();

        // Simulate a small access latency (e.g., 10ns)
        return clockPeriod();
    }

    /**
     * Why is it there?
For Atomic Mode (Fast Forwarding): When recvAtomic calls this function, it returns this value. The CPU adds this number to the simulation clock to simulate that the instruction store 0x1, 0x10000020 took time to execute.

For Timing Mode (Detailed): In your current code, this return value is ignored (since recvTimingReq sends the response immediately). If you wanted to be extremely precise, you would use this value to schedule the response 10ns into the future, but for configuration registers, responding immediately (0 latency) is standard practice and perfectly fine.
     */

    // ===========================================================================
    //                          THE AUTONOMOUS STATE MACHINE
    // ===========================================================================

    // --- 0. API: The Trigger (Doorbell) ---
    void IMCAccelerator::startProcessing(Addr wAddr, size_t wSz,
                                         Addr iAddr, size_t iSz,
                                         Addr oAddr)
    {
        if (state != IDLE)
        {
            warn("Accelerator busy! Ignoring request.");
            return;
        }

        DPRINTF(IMCAccelerator, "JOB START: Weights@%#x, Inputs@%#x -> SRAM -> Core -> SRAM -> DRAM\n", wAddr, iAddr);

        // Save Job Configuration
        weightDramAddr = wAddr;
        weightSize = wSz;
        inputDramAddr = iAddr;
        inputSize = iSz;
        outputDramAddr = oAddr;

        // 2. Reset Tiling Offsets
        currentInputOffset = 0;
        currentOutputOffset = 0;

        // 3. Explicitly reset the Core's accumulators
        // This prepares the hardware for a fresh Output Stationary run
        imcCore->resetAccumulators();

        // 4. Start Phase 1: Fetch Weights
        startFetchWeights();
    }

    // --- 1. Fetch Weights (DRAM -> Transit Buffer) ---
    void IMCAccelerator::startFetchWeights()
    {
        state = DMA_FETCHING_WEIGHTS;
        DPRINTF(IMCAccelerator, "[1/8] DMA: Fetching Weights...\n");

        dmaPort.dmaAction(MemCmd::ReadReq, weightDramAddr, weightSize, &dmaDoneEvent, transitBuffer, 0);
    }

    // --- 2. Fetch Inputs (DRAM -> Transit Buffer) ---
    void IMCAccelerator::startFetchInputs()
    {
        state = DMA_FETCHING_INPUTS;
        DPRINTF(IMCAccelerator, "[2/8] DMA: Fetching Inputs...\n");

        dmaPort.dmaAction(MemCmd::ReadReq, inputDramAddr, inputSize, &dmaDoneEvent, transitBuffer, 0);
    }

    // --- 3. Write Input SPAD (Transit Buffer -> SRAM) ---
    void IMCAccelerator::startSpadWriteInput()
    {
        state = WRITING_INPUT_SPAD;
        DPRINTF(IMCAccelerator, "[3/8] SRAM: Writing Inputs to Scratchpad...\n");

        // Copy from Transit Buffer to Packet
        RequestPtr req = std::make_shared<Request>(0x0, inputSize, 0, requestorId);
        PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
        pkt->allocate();
        pkt->setData(transitBuffer);

        if (!inputSpadPort.sendTimingReq(pkt))
        {
            DPRINTF(IMCAccelerator, "SRAM busy (Write Input). Waiting...\n");
            blockedPacket = pkt;
            // Return and wait for recvReqRetry
        }
    }

    // --- 4. Read Input SPAD (SRAM -> Transit Buffer -> Core) ---
    // We must read back from SRAM to simulate the Core loading data from Local Memory
    void IMCAccelerator::startSpadReadInput()
    {
        state = READING_INPUT_SPAD;

        // Ask the Core: "How much data fits in your drivers?"
        size_t chunkSize = imcCore->getInputDriverSize();

        // Safety: Ensure we don't read past the end of the data we have in SRAM
        size_t bytesRemaining = inputSize - currentInputOffset;
        size_t fetchSize = std::min(chunkSize, bytesRemaining);

        DPRINTF(IMCAccelerator, "[4/8] SRAM: Reading Tile (Size: %lu) from Offset %lu...\n",
                fetchSize, currentInputOffset);

        // Create request at the specific offset (currentInputOffset)
        RequestPtr req = std::make_shared<Request>(currentInputOffset, fetchSize, 0, requestorId);

        PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
        pkt->allocate();

        if (!inputSpadPort.sendTimingReq(pkt))
        {
            DPRINTF(IMCAccelerator, "SRAM busy (Read Input). Waiting...\n");
            blockedPacket = pkt;
        }
    }

    // --- 5. Compute (Core Execution) ---
    void IMCAccelerator::startCompute()
    {
        state = COMPUTING;
        DPRINTF(IMCAccelerator, "[5/8] CORE: Computing...\n");

        // [FIX] Calculate the valid data size for this specific tile
        size_t chunkSize = imcCore->getInputDriverSize();
        size_t validBytes = std::min(chunkSize, inputSize - currentInputOffset);

        // Create vector using ONLY the valid bytes in transitBuffer
        std::vector<uint8_t> iVec(transitBuffer, transitBuffer + validBytes);

        // (Note: Weights were loaded during dmaDone for DMA_FETCHING_WEIGHTS)
        imcCore->setInputs(iVec);

        // 1. Ask the core for the cycle count
        Cycles coreCycles = imcCore->getLatencyCycles();

        // 2. Convert Cycles to Ticks for the Statistics
        // ticksToCycles is a helper from ClockedObject
        Tick latencyTicks = clockPeriod() * (uint64_t)coreCycles;
        imcCore->addLatency(latencyTicks);

        // 3. Schedule the finish
        // We wait 1 cycle to 'latch' the start, then add the core's cycles
        schedule(computeEvent, clockEdge(coreCycles));

        DPRINTF(IMCAccelerator, "Core working for %d cycles. Finishing at tick %ld\n",
                (uint64_t)coreCycles, clockEdge(coreCycles));
    }

    void IMCAccelerator::processComputeEvent()
    {
        // Timer expired. Math is done.
        imcCore->execute();
        startSpadWriteOutput();
    }

    // --- 6. Write Output SPAD (Core Output -> SRAM) ---
    void IMCAccelerator::startSpadWriteOutput()
    {
        state = WRITING_OUTPUT_SPAD;

        // Ask Core for the exact accumulator size
        size_t writeSize = imcCore->getAccumulatorSize();

        DPRINTF(IMCAccelerator, "[6/8] SRAM: Writing Accumulator (Size: %lu) to Offset %lu...\n",
                writeSize, currentOutputOffset);

        const auto &results = imcCore->getOutput();

        // Copy Core Output -> Transit Buffer
        // Note: In a loop, we would handle the offset in transitBuffer too,
        // but for this single-chunk test, we copy to the start of the buffer.
        memcpy(transitBuffer, results.data(), writeSize);

        // [NEW] Create Request at currentOutputOffset
        RequestPtr req = std::make_shared<Request>(currentOutputOffset, writeSize, 0, requestorId);

        PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
        pkt->allocate();
        pkt->setData(transitBuffer);

        if (!outputSpadPort.sendTimingReq(pkt))
        {
            DPRINTF(IMCAccelerator, "SRAM busy. Holding packet.\n");
            blockedPacket = pkt;
        }
    }

    // --- 7. Read Output SPAD (SRAM -> Transit Buffer) ---
    void IMCAccelerator::startSpadReadOutput()
    {
        state = READING_OUTPUT_SPAD;

        // Use the accumulator size to know how much to read back for DMA
        size_t readSize = imcCore->getAccumulatorSize();

        DPRINTF(IMCAccelerator, "[7/8] SRAM: Reading Results for DMA...\n");

        // Read from the Output SRAM offset
        RequestPtr req = std::make_shared<Request>(currentOutputOffset, readSize, 0, requestorId);
        PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
        pkt->allocate();

        if (!outputSpadPort.sendTimingReq(pkt))
        {
            DPRINTF(IMCAccelerator, "SRAM busy (Read Output). Waiting...\n");
            blockedPacket = pkt;
        }
    }

    // --- 8. DMA Commit (Transit Buffer -> DRAM) ---
    void IMCAccelerator::startCommit()
    {
        state = DMA_COMMITTING;
        DPRINTF(IMCAccelerator, "[8/8] DMA: Committing Results to DRAM...\n");

        size_t outSize = imcCore->getOutput().size() * sizeof(int32_t);
        dmaPort.dmaAction(MemCmd::WriteReq, outputDramAddr, outSize, &dmaDoneEvent, transitBuffer, 0);
    }

    // ===========================================================================
    //                              CALLBACK HANDLERS
    // ===========================================================================

    void IMCAccelerator::dmaDone()
    {
        // The DMA "Truck" has arrived.

        if (state == DMA_FETCHING_WEIGHTS)
        {
            // Special Case: Weights go DIRECTLY to Core (Bypassing SRAM)
            DPRINTF(IMCAccelerator, "Weights Loaded. Loading Core Internal Weight Buffer.\n");
            std::vector<uint8_t> wVec(transitBuffer, transitBuffer + weightSize);
            imcCore->setWeights(wVec);

            // Next: Get Inputs
            startFetchInputs();
        }
        else if (state == DMA_FETCHING_INPUTS)
        {
            // Inputs must go to SRAM first
            startSpadWriteInput();
        }
        else if (state == DMA_COMMITTING)
        {
            DPRINTF(IMCAccelerator, "JOB COMPLETE.\n");
            state = IDLE;
        }
    }

    void IMCAccelerator::handleRetry()
    {
        if (blockedPacket == nullptr)
            return;

        DPRINTF(IMCAccelerator, "Retry: Resending blocked packet in state %d.\n", state);

        bool success = false;

        // Send to the correct port based on current state
        if (state == WRITING_INPUT_SPAD || state == READING_INPUT_SPAD)
        {
            success = inputSpadPort.sendTimingReq(blockedPacket);
        }
        else if (state == WRITING_OUTPUT_SPAD || state == READING_OUTPUT_SPAD)
        {
            success = outputSpadPort.sendTimingReq(blockedPacket);
        }

        if (success)
        {
            DPRINTF(IMCAccelerator, "Retry successful.\n");
            blockedPacket = nullptr;
        }
        else
        {
            DPRINTF(IMCAccelerator, "Retry failed again. Still waiting.\n");
        }
    }

    bool IMCAccelerator::SpadPort::recvTimingResp(PacketPtr pkt)
    {
        // 1. Finished WRITING Inputs to SRAM
        if (owner->state == WRITING_INPUT_SPAD && pkt->isWrite())
        {
            // Now we read them back into the core
            owner->startSpadReadInput();
        }
        // 2. Finished READING Inputs from SRAM
        else if (owner->state == READING_INPUT_SPAD && pkt->isRead())
        {
            // Data is in Packet. Move to TransitBuffer.
            memcpy(owner->transitBuffer, pkt->getPtr<uint8_t>(), pkt->getSize());
            // Start Math
            owner->startCompute();
        }
        // 3. Finished WRITING Result to SRAM
        else if (owner->state == WRITING_OUTPUT_SPAD && pkt->isWrite())
        {
            // Now read it back for DMA
            owner->startSpadReadOutput();
        }
        // 4. Finished READING Result from SRAM
        else if (owner->state == READING_OUTPUT_SPAD && pkt->isRead())
        {
            // Data is in Packet. Move to TransitBuffer.
            memcpy(owner->transitBuffer, pkt->getPtr<uint8_t>(), pkt->getSize());
            // DMA to DRAM
            owner->startCommit();
        }

        delete pkt;
        return true;
    }

} // namespace gem5