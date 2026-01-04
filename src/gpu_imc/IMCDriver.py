from m5.objects.Process import EmulatedDriver
from m5.params import *


class IMCDriver(EmulatedDriver):
    type = "IMCDriver"
    cxx_header = "gpu_imc/imc_driver.hh"
    cxx_class = "gem5::IMCDriver"
