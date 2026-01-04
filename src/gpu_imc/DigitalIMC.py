from m5.objects.Device import DmaDevice
from m5.params import *


class DigitalIMC(DmaDevice):
    type = "DigitalIMC"
    cxx_header = "gpu_imc/digital_imc.hh"
    cxx_class = "gem5::DigitalIMC"

# PIO Interface (Standard defaults are okay here)
    pio_addr = Param.Addr(0x80000000, "Base Address")
    pio_latency = Param.Latency("10ns", "Register Access Latency")

    # --- PHYSICAL SILICON CONSTRAINTS (No Defaults - Must be set in DSE) ---
    
    # 1. The Array Geometry
    num_rows = Param.Int("Number of Wordlines (Input Features)")
    num_cols = Param.Int("Number of Bitlines (Output Neurons)")
    
    # 2. The Cell Capability (Hardware Limits)
    max_weight_prec = Param.Int("Maximum bits per weight cell")
    
    # 3. The Input SRAM Geometry
    #    The SRAM Row Width is calculated as: max_input_prec * max_inputs_per_row
    max_input_prec = Param.Int("Physical Input Bit Width (Fixed Hardware)")
    max_inputs_per_row = Param.Int("Maximum number of inputs stored per row")
    
    # 4. Output Buffer
    output_buffer_depth = Param.Int(64, "Max number of output vectors to store")