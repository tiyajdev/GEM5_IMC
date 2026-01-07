import os
import sys

import m5
from m5.objects import *

# -------------------------------------------------------------------------
# 1. Setup System
# -------------------------------------------------------------------------
system = System()
system.clk_domain = SrcClockDomain(
    clock="1GHz", voltage_domain=VoltageDomain()
)
system.mem_mode = "timing"  # REQUIRED for DMA to function
system.mem_ranges = [AddrRange("512MB")]  # RAM: 0x00000000 -> 0x20000000

# -------------------------------------------------------------------------
# 2. CPU & Bus
# -------------------------------------------------------------------------
system.cpu = TimingSimpleCPU()
system.membus = SystemXBar()

# Connect CPU ports
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()

# -------------------------------------------------------------------------
# 3. Memory Controller
# -------------------------------------------------------------------------
# A. Main System Memory (0 - 512MB) for Stack/Heap/Code
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]  # 0x0 -> 0x20000000
system.mem_ctrl.port = system.membus.mem_side_ports

# -------------------------------------------------------------------------
# 4. Digital IMC Hardware
# -------------------------------------------------------------------------
# CRITICAL: pio_addr must match the address in 'imc_driver.hh' (0x80000000)
system.imc = DigitalIMC(
    pio_addr=0x80000000,
    pio_latency="10ns",
    num_rows=32,
    num_cols=6,
    num_banks=1,
    max_precision_w=4,  # Hardware fixed at 4-bit weights
    max_precision_i=8,  # Hardware fixed at 8-bit inputs
    input_buffer_size="4KiB",
    output_buffer_size="4KiB",
)

# Connect IMC to Bus
system.imc.pio = system.membus.mem_side_ports  # For CPU -> Registers
system.imc.dma = system.membus.cpu_side_ports  # For IMC -> RAM

# -------------------------------------------------------------------------
# 5. Process & Driver
# -------------------------------------------------------------------------
binary = os.path.abspath("workloads/test_imc")
system.workload = SEWorkload.init_compatible(binary)

# Create the Driver
# This links "/dev/imc" to the Physical Address 0x80000000
imc_driver = IMCDriver()
imc_driver.filename = "imc"

process = Process()
process.cmd = [binary]
process.drivers = [imc_driver]  # <--- Attach driver to process

system.cpu.workload = process
system.cpu.createThreads()

# -------------------------------------------------------------------------
# 6. Run Simulation
# -------------------------------------------------------------------------
root = Root(full_system=False, system=system)
m5.instantiate()

print(f"Beginning simulation with {binary}...")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
