from m5.params import *
from m5.objects.SimObject import SimObject

class IMCTester(SimObject):
    type = 'IMCTester'
    cxx_header = "imc_acc_model/IMCTester.hh"
    cxx_class = "gem5::IMCTester"

    core = Param.DigitalIMCCore("The core to test")
    
    # 0=Unpacked, 1=Packed, 2=Gating
    test_mode = Param.Int(0, "Test Mode: 0=Unpacked, 1=Packed, 2=Gating")