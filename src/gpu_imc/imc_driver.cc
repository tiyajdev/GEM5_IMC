// 1. Always include the corresponding header FIRST.
#include "gpu_imc/imc_driver.hh"
// 2. Required to access 'tc->getProcessPtr()'
#include "cpu/thread_context.hh"
// 3. Required to access 'process->fds->allocFD'
#include "sim/process.hh"
// 4. Required to create 'DeviceFDEntry'
#include "sim/fd_entry.hh"
#include "sim/mem_state.hh"  // Required for extendMmap
#include "mem/page_table.hh" // Required for pTable->map
// 5. Optional: Required if you use DPRINTF
// #include "base/trace.hh"

namespace gem5
{

    // Constructor
    IMCDriver::IMCDriver(const Params &p)
        : EmulatedDriver(p)
    {
    }

    // 1. OPEN: Safely allocate a File Descriptor
    int
    IMCDriver::open(ThreadContext *tc, int mode, int flags)
    {
        // Get the process that called open()
        auto process = tc->getProcessPtr();

        // Create a new entry in the file table pointing to this driver
        auto device_fd_entry = std::make_shared<DeviceFDEntry>(this, filename);

        // Ask the process to find the next FREE integer (e.g., 3, 4, 5...)
        // This guarantees we never accidentally overwrite stdin (0) or stdout (1).
        int tgt_fd = process->fds->allocFD(device_fd_entry);

        return tgt_fd;
    }

    // 2. IOCTL: Dummy implementation
    int
    IMCDriver::ioctl(ThreadContext *tc, unsigned req, Addr buf)
    {
        return 0;
    }

    // 3. MMAP: The Bridge to Hardware
    Addr
    IMCDriver::mmap(ThreadContext *tc, Addr start, uint64_t length,
                    int prot, int tgtFlags, int tgtFd, off_t offset)
    {
        // 1. Ask the System to find a free Virtual Address (The Table Number)
        auto process = tc->getProcessPtr();
        auto mem_state = process->memState;
        start = mem_state->extendMmap(length);

        // 2. CONNECT PAGE 1: The Registers
        //    Map Virtual "start" -> Physical 0x80000000
        process->pTable->map(start, 0x80000000, 4096,
                             EmulationPageTable::Uncacheable);

        // 3. CONNECT PAGE 2: The Data Buffer
        //    Map Virtual "start + 4096" -> Physical 0x88000000
        //    We only do this if the user asked for enough space.
        if (length > 4096)
        {
            process->pTable->map(start + 4096, 0x10000000, 4096,
                                 EmulationPageTable::Uncacheable);
        }

        return start;
    }

} // namespace gem5
