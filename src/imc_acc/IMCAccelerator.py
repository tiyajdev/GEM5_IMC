from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject

class IMCAccelerator(ClockedObject):
    type = 'IMCAccelerator'
    cxx_header = "imc_acc/imc_accelerator.hh"
    cxx_class = "gem5::IMCAccelerator"

    # =========================================================================
    # 1. Hardware Ports
    # =========================================================================
    
    # [NEW] Control Port (Slave/Response)
    # The CPU connects here to write to the MMIO registers.
    pio_port = ResponsePort("MMIO Control Port for Configuration")

    # The DMA Port connects to the System Bus (SystemXBar)
    # It acts as a "Master" to fetch data from DRAM.
    dma_port = RequestPort("DMA Port for fetching weights/inputs and writing results")

    # The Scratchpad Ports connect to local SRAMs (SimpleMemory or Cache)
    # They act as "Masters" to read/write local temporary buffers.
    input_spad_port = RequestPort("Port to the Input Scratchpad")
    output_spad_port = RequestPort("Port to the Output Scratchpad")

    # Customizable Memory Map
    pio_addr = Param.Addr(0x30000000, "Base address for MMIO registers")
    pio_size = Param.Addr(0x1000, "Size of the MMIO address range (default 4KB)")

    # =========================================================================
    # 2. Components & System
    # =========================================================================

    # Link to the Math Core (DigitalIMCCore)
    imc_core = Param.DigitalIMCCore("The internal computation unit")

    # Pointer to the global System object (Required for DMA)
    system = Param.System(Parent.any, "System this device is part of")