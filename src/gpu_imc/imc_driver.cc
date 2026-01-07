// 1. Always include the corresponding header FIRST.
#include "gpu_imc/imc_driver.hh"
// 2. Required to access 'tc->getProcessPtr()'
#include "cpu/thread_context.hh"
// 3. Required to access 'process->fds->allocFD'
#include "sim/process.hh"
// 4. Required to create 'DeviceFDEntry'
#include "sim/fd_entry.hh"
#include "sim/mem_state.hh"                 // Required for extendMmap
#include "mem/page_table.hh"                // Required for pTable->map
#include "mem/se_translating_port_proxy.hh" // Required for reading user memory
#include "debug/DigitalIMC.hh"

namespace gem5
{
    // Define the data structure we share with the User App
    struct imc_ioctl_args
    {
        uint64_t va_addr; // Input: Virtual Address
        uint64_t pa_addr; // Output: Physical Address
    };

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
        // Helper to safely read/write the User's memory
        SETranslatingPortProxy se_proxy(tc);

        // COMMAND 0xCAFE: VIRTUAL TO PHYSICAL TRANSLATION
        if (req == 0xCAFE)
        {
            imc_ioctl_args args;

            // 1. Read the struct from User Memory (User passed a pointer 'buf')
            se_proxy.readBlob(buf, &args, sizeof(args));

            DPRINTF(DigitalIMC, "Driver: Request to translate VA 0x%lx\n", args.va_addr);

            // 2. Perform the Translation
            Addr physical_addr = 0;
            auto process = tc->getProcessPtr();

            if (process->pTable->translate(args.va_addr, physical_addr))
            {
                // Success! Write the result into the struct
                args.pa_addr = (uint64_t)physical_addr;
                DPRINTF(DigitalIMC, "Driver: Success! VA 0x%lx -> PA 0x%lx\n", args.va_addr, args.pa_addr);
            }
            else
            {
                // Failure
                DPRINTF(DigitalIMC, "Driver: Translation Failed for VA 0x%lx\n", args.va_addr);
                return -1;
            }

            // 3. Write the struct BACK to User Memory
            se_proxy.writeBlob(buf, &args, sizeof(args));

            return 0; // Return 0 for Success (Standard Linux convention)
        }

        return 0;
    }

    // 3. MMAP: The Bridge to Hardware
    Addr IMCDriver::mmap(ThreadContext *tc, Addr start, uint64_t length,
                         int prot, int tgtFlags, int tgtFd, off_t offset)
    {
        auto process = tc->getProcessPtr();
        auto mem_state = process->memState;

        // 1. Allocate Virtual Address Space
        // This asks the OS (Gem5) for a chunk of free virtual memory.
        start = mem_state->extendMmap(length);

        // 2. Map Virtual Pointer -> Physical Register Base
        // CRITICAL: This physical address (0x80000000) MUST match the 'pio_addr'
        // you define in your Python run script (run_imc.py).
        // If they differ, the CPU writes will miss the device!
        process->pTable->map(start, 0x80000000, length,
                             EmulationPageTable::Uncacheable);

        return start;
    }

} // namespace gem5
