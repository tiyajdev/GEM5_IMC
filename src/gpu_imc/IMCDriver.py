from m5.params import *
from m5.objects.Process import EmulatedDriver

class IMCDriver(EmulatedDriver):
    type = 'IMCDriver'
    cxx_header = "gpu_imc/imc_driver.hh"
    cxx_class = "gem5::IMCDriver"