/*
 * Copyright (c) 2024 IMC Accelerator Model
 * All rights reserved.
 */

#include "imc_acc_model/IMC_DMA_Engine.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "sim/system.hh"
#include "debug/IMCAcc.hh" // Optional: For DPRINTF if you enabled the flag

namespace gem5
{

    IMC_DMA_Engine::IMC_DMA_Engine(const Params &p)
        : DmaDevice(p),
          core(p.core),
          pioAddr(p.pio_addr),
          pioDelay(p.pio_latency),
          srcAddr(0), dstAddr(0), weightAddr(0), transferSize(0),
          bankID(0),
          activeCols(16),
          busy(false),
          lastCommand(0),
          dmaReadEvent([this]
                       { dmaReadDone(); }, name()),
          computeEvent([this]
                       { computeDone(); }, name()),
          dmaWriteEvent([this]
                        { dmaWriteDone(); }, name())
    {
        // Allocate internal buffer (64KB)
        bufferSize = 65536;
        buffer = new uint8_t[bufferSize];
    }

    IMC_DMA_Engine::~IMC_DMA_Engine()
    {
        delete[] buffer;
    }

    // =============================================================================
    //                            PIO INTERFACE (CPU SIDE)
    // =============================================================================

    AddrRangeList
    IMC_DMA_Engine::getAddrRanges() const
    {
        // Report our address range to the system (256 bytes)
        return {RangeSize(pioAddr, 0x100)};
    }

    Tick
    IMC_DMA_Engine::read(PacketPtr pkt)
    {
        // Calculate offset from base address
        Addr offset = pkt->getAddr() - pioAddr;

        // Status Register (Offset 0x28)
        if (offset == 0x28)
        {
            pkt->setLE<uint64_t>(busy ? 1 : 0);
        }
        else
        {
            // Return 0 for all other registers on read
            pkt->setLE<uint64_t>(0);
        }

        pkt->makeAtomicResponse();
        return pioDelay;
    }

    Tick
    IMC_DMA_Engine::write(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        uint64_t data = pkt->getLE<uint64_t>();

        switch (offset)
        {
        case 0x00:
            srcAddr = data;
            break;
        case 0x08:
            dstAddr = data;
            break;
        case 0x10:
            weightAddr = data;
            break;
        case 0x18:
            // This is "transferSize", which maps to "Active Rows" (Inputs)
            transferSize = data;
            break;
        case 0x30:
            bankID = (int)data;
            break;
            // CPU writes how many filters (columns) are active for this tile
        case 0x38:
            activeCols = (int)data;
            break;
        case 0x20: // COMMAND REGISTER
            if (!busy)
            {
                lastCommand = data;
                busy = true;

                if (data == 1)
                {
                    // CMD 1: LOAD WEIGHTS (Read from weightAddr)
                    dmaPort.dmaAction(MemCmd::ReadReq, weightAddr, transferSize, &dmaReadEvent, buffer, 0);
                }
                else if (data == 2)
                {
                    // CMD 2: COMPUTE (Read from srcAddr)
                    dmaPort.dmaAction(MemCmd::ReadReq, srcAddr, transferSize, &dmaReadEvent, buffer, 0);
                }
            }
            break;
        }

        pkt->makeAtomicResponse();
        return pioDelay;
    }

    // =============================================================================
    //                            DMA LOGIC (ACCELERATOR SIDE)
    // =============================================================================

    void IMC_DMA_Engine::dmaReadDone()
    {
        std::vector<uint8_t> dataVec(buffer, buffer + transferSize);

        if (lastCommand == 1)
        {
            // --- WEIGHT LOADING MODE ---
            // Pass the bankID to the Core!
            core->setWeights(dataVec, bankID, activeCols); // <--- FIXED
            busy = false;
        }
        else if (lastCommand == 2)
        {
            // --- COMPUTE MODE ---
            // Tell Core which bank to use for calculation
            core->setComputeBank(bankID); // <--- FIXED

            core->setInputs(dataVec);

            // ---------------------------------------------------------
            // 2. CRITICAL FIX: SET DIMENSIONS FOR GATING
            // ---------------------------------------------------------
            // activeCols: Set by REG 0x38 (default 16)
            // activeRows: Derived from REG 0x18 (transferSize)
            core->setActiveDimensions(activeCols, (int)transferSize);

            core->execute();

            Cycles latency = core->getLatencyCycles();
            schedule(computeEvent, clockEdge(latency));
        }
    }

    void
    IMC_DMA_Engine::computeDone()
    {
        // 1. Get the results from the Core
        const std::vector<int32_t> &results = core->getOutput();

        // 2. Copy results into the DMA buffer
        // Note: We need to serialize the int32 results into the uint8 buffer.
        // Ensure we don't overflow the buffer.
        size_t resultBytes = results.size() * sizeof(int32_t);

        if (resultBytes > bufferSize)
        {
            panic("IMC_DMA_Engine: Result size (%d) exceeds DMA buffer (%d)!", resultBytes, bufferSize);
        }

        // Manual serialization (Little Endian)
        for (size_t i = 0; i < results.size(); ++i)
        {
            int32_t val = results[i];
            buffer[i * 4 + 0] = (val >> 0) & 0xFF;
            buffer[i * 4 + 1] = (val >> 8) & 0xFF;
            buffer[i * 4 + 2] = (val >> 16) & 0xFF;
            buffer[i * 4 + 3] = (val >> 24) & 0xFF;
        }

        // 3. Write back to DRAM
        // We use 'resultBytes' as the size, not 'transferSize' (which was the input size)
        // Make sure your C code allocated enough space at dstAddr!
        dmaPort.dmaAction(MemCmd::WriteReq, dstAddr, resultBytes, &dmaWriteEvent, buffer, 0);
    }

    void
    IMC_DMA_Engine::dmaWriteDone()
    {
        // Write complete. Accelerator is now Idle.
        busy = false;
    }

} // namespace gem5