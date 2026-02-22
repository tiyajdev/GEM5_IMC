#ifndef __IMC_ACC_MODEL_IMC_DMA_ENGINE_HH__
#define __IMC_ACC_MODEL_IMC_DMA_ENGINE_HH__

#include "dev/dma_device.hh"
#include "params/IMC_DMA_Engine.hh"
#include "imc_acc_model/digital_imc_core.hh"

namespace gem5
{

    class IMC_DMA_Engine : public DmaDevice
    {
    private:
        // --- Hardware Links ---
        DigitalIMCCore *core;

        // --- Configuration Variables ---
        Addr pioAddr;
        Tick pioDelay;

        // --- Registers (Memory Map) ---
        Addr srcAddr;
        Addr dstAddr;
        Addr weightAddr;
        uint32_t transferSize;
        int bankID;
        int activeCols;
        int activeRows;

        bool busy;
        int lastCommand;

        // --- Internal Buffers ---
        uint8_t *buffer;
        uint32_t bufferSize;

        // --- Event Handlers ---
        void dmaReadDone();
        EventFunctionWrapper dmaReadEvent;

        void computeDone();
        EventFunctionWrapper computeEvent;

        void dmaWriteDone();
        EventFunctionWrapper dmaWriteEvent;

    public:
        // This typedef is required for the Params to work correctly
        typedef IMC_DMA_EngineParams Params;
        const Params &params() const { return (const Params &)_params; }

        IMC_DMA_Engine(const Params &p);
        ~IMC_DMA_Engine();

        // --- PIO Interface (CPU -> Accelerator) ---
        Tick read(PacketPtr pkt) override;
        Tick write(PacketPtr pkt) override;
        AddrRangeList getAddrRanges() const override;
    };

} // namespace gem5

#endif // __IMC_ACC_MODEL_IMC_DMA_ENGINE_HH__