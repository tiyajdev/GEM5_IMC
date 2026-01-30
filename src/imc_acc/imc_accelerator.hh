#ifndef __IMC_ACC_IMC_ACCELERATOR_HH__
#define __IMC_ACC_IMC_ACCELERATOR_HH__

#include "mem/port.hh"
#include "mem/packet.hh" // <--- ADD THIS (Defines Packet & PacketPtr)
#include "base/types.hh"
#include "sim/clocked_object.hh"
#include "dev/dma_device.hh" // Required for DmaPort
#include "sim/system.hh"
#include "params/IMCAccelerator.hh"
#include "imc_acc/digital_imc_core.hh"
#include "mem/packet_access.hh"

namespace gem5
{

    class IMCAccelerator : public ClockedObject
    {
    private:
        // Memory Map Configuration
        const Addr pioAddr;
        const Addr pioSize;
        // --- [NEW] Energy Parameter for Simultaneous Write ---
        const double energyPerBitWriteFj;
        // ======================================================================
        // 0. RETRY LOGIC VARIABLES (Must be in the Main Class)
        // ======================================================================
        // The "Waiting Room" for a blocked packet.
        // It lives here so the State Machine (startSpadWriteInput etc.) can access it.
        PacketPtr blockedPacket;

        // The helper function that the Port calls when space opens up.
        void handleRetry();

        // ======================================================================
        // 1. PRIVATE PORTS (Internal Interface)
        //    These allow the accelerator to talk to its local SRAMs.
        // ======================================================================
        class SpadPort : public RequestPort
        {
        private:
            IMCAccelerator *owner;

        public:
            // Constructor: Note we only pass 'name' to the base RequestPort
            // to avoid the deprecation warning. We handle 'owner' ourselves.
            SpadPort(const std::string &name, IMCAccelerator *owner) : RequestPort(name), owner(owner) {}

            bool recvTimingResp(PacketPtr pkt) override;
            void recvReqRetry() override { owner->handleRetry(); }
        };

        // Instances of the custom ports
        SpadPort inputSpadPort;
        SpadPort outputSpadPort;

        // ======================================================================
        // [NEW] 1.5 CONTROL PORT (Slave Interface / MMIO)
        //       The CPU writes to this port to configure registers.
        // ======================================================================
        class CommandPort : public ResponsePort
        {
        private:
            IMCAccelerator *owner;

            //  Packet waiting to be sent (Holds the response during the delay)
            PacketPtr pendingPacket = nullptr;

            //  The Event object that triggers the response sending
            EventFunctionWrapper sendEvent;

        public:
            CommandPort(const std::string &name, IMCAccelerator *owner) : ResponsePort(name), owner(owner),
                                                                          sendEvent([this]
                                                                                    { trySendPacket(); }, name) {}

            AddrRangeList getAddrRanges() const override;
            Tick recvAtomic(PacketPtr pkt) override;
            void recvFunctional(PacketPtr pkt) override;
            bool recvTimingReq(PacketPtr pkt) override;
            void recvRespRetry() override;
            // [NEW] Helper function called by the Event
            void trySendPacket();
        };

        CommandPort commandPort;

        // ======================================================================
        // 2. PUBLIC PORT (System Interface)
        //    The 'DmaPort' acts as the Autonomous Controller. It handles the
        //    System Bus protocol, retries, and chunking automatically.
        // ======================================================================
        DmaPort dmaPort;

        // ======================================================================
        // 3. COMPONENTS & DATA
        // ======================================================================
        DigitalIMCCore *imcCore; // The Math Logic
        System *system;          // The Global System Map (needed for DMA)

        RequestorID requestorId;

        // Internal Registers (The destination for MMIO writes)
        Addr regWeightAddr;
        Addr regInputAddr;
        Addr regOutputAddr;
        uint32_t regSize;

        // Job Configuration (Registers set by CPU/Python)
        Addr weightDramAddr;
        Addr inputDramAddr;
        Addr outputDramAddr;

        size_t weightSize;
        size_t inputSize;

        // Loop Counters / Tiling Offsets
        // Tracks where we are reading from in the Input Scratchpad
        size_t currentInputOffset;

        // Tracks where we are writing to in the Output Scratchpad
        size_t currentOutputOffset;

        // The Hardware "Transit Buffer" (FIFO/Staging Buffer)
        // Physically, this is a set of registers/SRAM that holds data
        // while it crosses the clock domain between the System Bus and the Local SRAM.
        uint8_t *transitBuffer;

        // ======================================================================
        // 4. CONTROL STATE MACHINE
        //    Defines the strict lifecycle of the data.
        // ======================================================================
        enum State
        {
            IDLE,
            DMA_FETCHING_WEIGHTS,
            DMA_FETCHING_INPUTS,
            WRITING_INPUT_SPAD,
            READING_INPUT_SPAD,
            COMPUTING,
            WRITING_OUTPUT_SPAD,
            READING_OUTPUT_SPAD,
            DMA_COMMITTING
        };
        State state;

        // ======================================================================
        // 5. EVENTS & HANDLERS
        // ======================================================================

        // Event wrapper for the math latency
        void processComputeEvent();
        EventFunctionWrapper computeEvent;

        // Callback wrapper for when the DMA finishes a transfer
        void dmaDone();
        EventFunctionWrapper dmaDoneEvent;

        // --- State Transition Helpers ---
        void startFetchWeights();
        void startFetchInputs();
        void startSpadWriteInput();
        void startSpadReadInput();
        void startCompute();
        void startSpadWriteOutput();
        void startSpadReadOutput();
        void startCommit();

    public:
        // Standard Gem5 Boilerplate
        IMCAccelerator(const IMCAcceleratorParams &p);
        ~IMCAccelerator();

        Port &getPort(const std::string &if_name,
                      PortID idx = InvalidPortID) override;

        void startup() override;
        Tick handleRegisterAccess(PacketPtr pkt);
        // ======================================================================
        //  REQUIRED FIXES FOR ADDRESS HANDSHAKE
        // ======================================================================

        // 1. Define the Init function to trigger the "Hello" signal to the bus
        void init() override;

        // 2. Define the Helper to return the [0x30000000 - 0x30002000] range
        AddrRangeList getAddrRanges() const;

        // ======================================================================

        // ======================================================================
        // 6. API (The Driver Interface)
        //    The CPU calls this to start the autonomous job.
        // ======================================================================
        void startProcessing(Addr wAddr, size_t wSz,
                             Addr iAddr, size_t iSz,
                             Addr oAddr);
    };

} // namespace gem5

#endif // __IMC_ACC_IMC_ACCELERATOR_HH__