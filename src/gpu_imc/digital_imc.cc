#include "gpu_imc/digital_imc.hh"

#include "base/trace.hh"
#include "debug/DigitalIMC.hh"
#include "mem/packet_access.hh"

namespace gem5
{

    DigitalIMC::DigitalIMC(const Params &p)
        : DmaDevice(p),
          pioAddr(p.pio_addr),
          pioSize(0x1000), // 4KB Address Space
          pioDelay(p.pio_latency),
          // Initialize Hardware Specs from Python
          numRows(p.num_rows),
          numCols(p.num_cols),
          numBanks(p.num_banks),
          maxPrecision(p.max_precision),
          // Initialize Events with Lambda wrappers
          computeDoneEvent([this]
                           { computeDone(); }, name()),
          dmaDoneEvent([this]
                       { dmaDone(); }, name())
    {
        // Initialize Registers to 0
        regs.cmd = 0;
        regs.status = 0;
        regs.prec = 0; // defined by the user
        regs.srcAddr = 0;
        regs.len = 0;

        DPRINTF(DigitalIMC, "Created DigitalIMC with %d Rows, %d Cols\n", numRows, numCols);
    }

    AddrRangeList
    DigitalIMC::getAddrRanges() const
    {
        return {RangeSize(pioAddr, pioSize)};
    }

    Tick
    DigitalIMC::read(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        unsigned size = pkt->getSize();
        uint64_t data = 0;

        // 1. Select the Register
        switch (offset)
        {
        case REG_CMD:
            data = regs.cmd;
            break;
        case REG_STATUS:
            data = regs.status;
            break;
        case REG_PREC:
            data = regs.prec;
            break;

        // 64-bit Register
        case REG_SRC_ADDR:
            data = regs.srcAddr;
            break;

        case REG_LEN:
            data = regs.len;
            break;

        default:
            data = 0; // Return 0 for gaps/padding
            // DPRINTF(DigitalIMC, "Read from invalid/padding offset 0x%x\n", offset);
            break;
        }

        // 2. Format the Output based on Request Size (lw vs ld)
        if (size == 4)
        {
            pkt->setLE<uint32_t>((uint32_t)data);
        }
        else if (size == 8)
        {
            pkt->setLE<uint64_t>(data);
        }
        else
        {
            panic("DigitalIMC: Read size %d not supported!\n", size);
        }

        pkt->makeResponse();
        return pioDelay;
    }

    Tick
    DigitalIMC::write(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        unsigned size = pkt->getSize();
        uint64_t data = 0;

        // --- NEW: Handle OS Cleanup (munmap) ---
        // The OS uses 64-byte writes to clear memory when the program ends.
        // We must ignore these to prevent a crash.
        if (size == 64)
        {
            DPRINTF(DigitalIMC, "Ignored 64-byte OS cleanup write at offset 0x%x\n", offset);
            pkt->makeResponse();
            return pioDelay;
        }

        // 1. Extract Data safely (handle 32-bit vs 64-bit writes)
        if (size == 4)
        {
            data = pkt->getLE<uint32_t>();
        }
        else if (size == 8)
        {
            data = pkt->getLE<uint64_t>();
        }
        else
        {
            panic("DigitalIMC: Write size %d not supported!\n", size);
        }

        // 2. Update Register
        switch (offset)
        {
        case REG_CMD:
            regs.cmd = (uint32_t)data;
            // Command Trigger Logic
            if (regs.cmd == 1)
                startCompute();
            if (regs.cmd == 2)
                startDma();
            break;

        case REG_PREC:
            regs.prec = (uint32_t)data;
            DPRINTF(DigitalIMC, "Config: Precision set to %d-bit\n", regs.prec);
            break;

        case REG_SRC_ADDR:
            if (size == 8)
            {
                regs.srcAddr = data;
            }
            else
            {
                // Handle 32-bit write to 64-bit reg (safety mask)
                regs.srcAddr = (regs.srcAddr & 0xFFFFFFFF00000000) | (uint32_t)data;
            }
            DPRINTF(DigitalIMC, "Config: DMA Address set to 0x%lx\n", regs.srcAddr);
            break;

        case REG_LEN:
            regs.len = (uint32_t)data;
            break;

        default:
            // Ignore writes to read-only regs (STATUS) or gaps
            break;
        }

        pkt->makeResponse();
        return pioDelay;
    }

    // --- COMMAND 1: COMPUTE LOGIC ---

    void
    DigitalIMC::startCompute()
    {
        if (regs.status != 0)
        {
            warn("DigitalIMC: Attempted to start compute while busy!\n");
            return;
        }

        DPRINTF(DigitalIMC, "CMD: Starting Compute (Prec=%d)...\n", regs.prec);
        regs.status = 1; // Mark Busy

        // TIMING MODEL:
        // For Bit-Serial (Chih et al.), latency scales linearly with precision.
        // 1 cycle per bit-plane.
        Tick latency = clockPeriod() * regs.prec;

        // Schedule the "Done" event
        schedule(computeDoneEvent, curTick() + latency);
    }

    void
    DigitalIMC::computeDone()
    {
        DPRINTF(DigitalIMC, "Compute Complete.\n");
        regs.status = 0; // Mark Idle
        regs.cmd = 0;    // Clear command

        // --- ANALYTICAL ENERGY MODEL (Sun et al. Table I) ---
        // We calculate the counts here. You can apply the fJ/MAC factor in your Python script or here.

        // 1. Logic Ops (NAND gates in the array)
        //    Total MACs = Rows * Cols * 1 (vector op)
        //    Since it is bit-serial, we did this 'precision' times.
        uint64_t total_macs = numRows * numCols;

        // 2. Adder Tree Operations (Accumulation)
        //    Each column has an adder tree of size (Rows).
        //    Num Adders per Tree approx = NumRows - 1
        uint64_t active_adders = (numRows - 1) * numCols;

        DPRINTF(DigitalIMC, "STATS: Precision=%d | Total MAC Ops=%lu | Active Adders=%lu\n",
                regs.prec, total_macs * regs.prec, active_adders * regs.prec);
    }

    // --- COMMAND 2: DMA LOGIC ---

    void
    DigitalIMC::startDma()
    {
        if (regs.status != 0)
        {
            warn("DigitalIMC: Attempted to start DMA while busy!\n");
            return;
        }

        if (regs.len == 0)
        {
            warn("DigitalIMC: DMA Length is 0!\n");
            return;
        }

        if (regs.srcAddr == 0)
        {
            warn("DigitalIMC: DMA Source Address is 0! Did you forget to set it?\n");
            return;
        }

        DPRINTF(DigitalIMC, "CMD: Starting DMA Load (Addr=0x%lx, Len=%d)...\n", regs.srcAddr, regs.len);
        regs.status = 1; // Mark Busy

        // 1. Prepare Buffer
        weight_array.resize(regs.len);

        // 2. Trigger DMA Read
        //    This sends a request to the memory bus. When it returns, dmaDone() is called.
        dmaPort.dmaAction(MemCmd::ReadReq,
                          regs.srcAddr,
                          regs.len,
                          &dmaDoneEvent,
                          weight_array.data(), // Pointer to internal vector storage
                          0);                  // delay
    }

    void
    DigitalIMC::dmaDone()
    {
        DPRINTF(DigitalIMC, "DMA Load Complete. Loaded %d bytes.\n", regs.len);
        regs.status = 0; // Mark Idle
        regs.cmd = 0;

        // Optional: Print first few bytes to verify
        if (weight_array.size() >= 4)
        {
            DPRINTF(DigitalIMC, "DATA[0-3]: %02x %02x %02x %02x\n",
                    weight_array[0], weight_array[1], weight_array[2], weight_array[3]);
        }
    }

} // namespace gem5