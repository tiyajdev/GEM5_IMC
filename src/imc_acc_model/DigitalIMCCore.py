from m5.params import *
from m5.objects.ClockedObject import ClockedObject

class DigitalIMCCore(ClockedObject):
    type = 'DigitalIMCCore'
    cxx_header = "imc_acc_model/digital_imc_core.hh"
    cxx_class = 'gem5::DigitalIMCCore'

    # --- 1. Dimensions (Architecture) ---
    di = Param.Int(64, "Number of Inputs (Rows) to the IMC")
    do_dim = Param.Int(64, "Number of Outputs (Columns) per Macro")
    m_batches = Param.Int(1, "Multiplexing Factor (Batches per Cycle)")
    num_macros = Param.Int(1, "Number of Macros (Total Cols = do_dim * num_macros)")
    num_weight_banks = Param.Int(2, "Number of Weight Banks (Double Buffering)")

    # --- 2. Precision & Timing ---
    bi = Param.Int(4, "Input Bit Precision")
    bw = Param.Int(4, "Weight Bit Precision")
    bcycles = Param.Int(1, "Number of bits processed per cycle (Throughput)")
    
    # --- 3. Logic Configuration ---
    input_signed = Param.Bool(False, "True if Input is Signed (Int8/4), False if Unsigned")
    weight_signed = Param.Bool(True, "True if Weight is Signed")

    # --- 4. Power Model ---
    energy_per_mac_fj = Param.Float(100.0, "Dynamic Energy per MAC operation (Femtojoules)")