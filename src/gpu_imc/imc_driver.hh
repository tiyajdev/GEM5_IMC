#ifndef __GPU_IMC_IMC_DRIVER_HH__
#define __GPU_IMC_IMC_DRIVER_HH__

// 1. Required for inheritance (public EmulatedDriver)
#include "sim/emul_driver.hh"
// 2. Required for the Params typedef (IMCDriverParams)
#include "params/IMCDriver.hh"

namespace gem5
{

    class IMCDriver : public EmulatedDriver
    {
    public:
        typedef IMCDriverParams Params;
        IMCDriver(const Params &p);

        /**
         * Open the device.
         * We override this to allocate a new file descriptor safely.
         */
        int open(ThreadContext *tc, int mode, int flags) override;

        /**
         * IO Control.
         * HANDLES ADDRESS TRANSLATION (Virtual -> Physical).
         */
        int ioctl(ThreadContext *tc, unsigned req, Addr buf) override;

        /**
         * Memory Map.
         * Maps the virtual pointer to our physical accelerator address.
         */
        Addr mmap(ThreadContext *tc, Addr start, uint64_t length,
                  int prot, int tgtFlags, int tgtFd, off_t offset) override;
    };

} // namespace gem5

#endif // __GPU_IMC_IMC_DRIVER_HH__