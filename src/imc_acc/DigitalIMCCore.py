from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject

class DigitalIMCCore(ClockedObject):
    type = 'DigitalIMCCore'
    cxx_header = "imc_acc/digital_imc_core.hh"
    cxx_class = "gem5::DigitalIMCCore"

    # Architecture params
    di = Param.Int(128, "Active rows (Input Activations/Di)")
    do_dim = Param.Int(128, "Active columns (Partial Sum Outputs/Do)")
    m_batches = Param.Int(1, "Multiplexing/Batches (M)")
    num_macros = Param.Int(1, "Number of Macros")

    # --- Memory & Buffering ---
    num_weight_banks = Param.Int(4, "Number of Weight Banks (Ping-Pong/Deep Buffering)")

    # Precision
    bi = Param.Int(8, "Input Precision")
    bw = Param.Int(8, "Weight Precision")

    # NEW PARAMETER
    bcycles = Param.Int(1, "Number of bits processed per cycle (Serial vs Parallel)")
    
    energy_per_mac_fj = Param.Float(0.5, "Energy per MAC (fJ)")
    energy_per_bit_write_fj = Param.Float(5.0, "Energy per bit SRAM write (fJ)")
    
    # Logic control
    input_signed = Param.Bool(False, "True if inputs use signed arithmetic (e.g. Leaky ReLU)")
    weight_signed = Param.Bool(True, "True if weights use signed arithmetic")
    mask = Param.UInt32(0xFF, "Data Mask")
