from m5.params import *
from m5.objects.Device import DmaDevice

class DigitalIMC(DmaDevice):
    type = 'DigitalIMC'
    cxx_header = "gpu_imc/digital_imc.hh"
    cxx_class = "gem5::DigitalIMC"

    # --- 1. Communication Interfaces ---
    # pio_addr and pio_latency are required because we override them
    # but 'system' is inherited automatically!
    pio_addr = Param.Addr("Address for MMIO control registers")
    pio_latency = Param.Latency("10ns", "Time required for CPU to read/write registers")

    # --- 2. Fixed Architectural Parameters ---
    num_rows = Param.Int(32, "Number of rows (Input Parallelism / Di)")
    num_cols = Param.Int(6, "Number of output columns (Output Parallelism / Do)")
    num_banks = Param.Int(1, "Number of physical parallel banks")
    max_precision = Param.Int(8, "Maximum supported bit precision (Physical Constraints)")