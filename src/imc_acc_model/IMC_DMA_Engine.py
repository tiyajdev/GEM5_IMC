from m5.params import *
from m5.proxy import *
from m5.objects.Device import DmaDevice

class IMC_DMA_Engine(DmaDevice):
    type = 'IMC_DMA_Engine'
    cxx_header = "imc_acc_model/IMC_DMA_Engine.hh"
    cxx_class = 'gem5::IMC_DMA_Engine'

    core = Param.DigitalIMCCore("The internal IMC Core model")

    pio_addr = Param.Addr("Device Address")
    pio_latency = Param.Latency("10ns", "Programmed IO latency")