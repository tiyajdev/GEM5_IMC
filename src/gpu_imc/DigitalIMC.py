from m5.params import *
from m5.proxy import *
from m5.objects.Device import DmaDevice # Corrected import

class DigitalIMC(DmaDevice):
    type = 'DigitalIMC'
    cxx_header = "gpu_imc/digital_imc.hh"
    cxx_class = "gem5::DigitalIMC"

    # --- 1. Communication Interfaces ---
    pio_addr = Param.Addr("Address for MMIO control registers")
    pio_latency = Param.Latency("10ns", "Time required for CPU to read/write registers")

    # --- 2. Fixed Hardware Geometry ---
    num_rows = Param.Int(32, "Number of rows (Input Parallelism / Di)")
    num_cols = Param.Int(6, "Number of output columns (Output Parallelism / Do)")
    num_banks = Param.Int(1, "Number of physical parallel banks")

    # --- 3. Precision Limits ---
    max_precision_w = Param.Int(8, "Max bit-width for Weights (e.g. 4 for W4A8)")
    max_precision_i = Param.Int(8, "Max bit-width for Inputs  (e.g. 8 for W4A8)")

    # --- 4. Buffer Sizes (CRITICAL: Added these to match C++) ---
    input_buffer_size = Param.MemorySize("4KiB", "Size of Input SRAM")
    output_buffer_size = Param.MemorySize("4KiB", "Size of Output SRAM")