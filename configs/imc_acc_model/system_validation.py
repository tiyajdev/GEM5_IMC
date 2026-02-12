import m5
from m5.objects import *
from m5.util import addToPath
import os, sys

# -------------------------------------------------------------------------
# 1. SETUP SYSTEM & CLOCK
# -------------------------------------------------------------------------
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
system.mem_mode = 'timing' # Mandatory for latency measurement
system.mem_ranges = [AddrRange('512MB')]

# -------------------------------------------------------------------------
# 2. CREATE CPU (RISC-V)
# -------------------------------------------------------------------------
system.cpu = RiscvTimingSimpleCPU()

# -------------------------------------------------------------------------
# 3. CREATE SYSTEM BUS (Crossbar)
# -------------------------------------------------------------------------
system.membus = SystemXBar()

# Connect CPU Cache Ports (Direct to bus for baremetal simplicity)
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# -------------------------------------------------------------------------
# 4. INSTANTIATE YOUR ACCELERATOR (IMC_DMA_Engine)
# -------------------------------------------------------------------------
# NEW (Safe - Above 512MB) no conflict with DRAM
system.imc = IMC_DMA_Engine()
system.imc_clk_domain = SrcClockDomain(clock='1.49GHz', voltage_domain=VoltageDomain())

system.imc.clk_domain = system.imc_clk_domain
system.imc.pio_addr = 0x30000000
system.imc.pio_latency = '10ns'

# 4a. Connect the Internal Math Core
system.imc.core = DigitalIMCCore()
system.imc.core.di = 144        # Rows (Input Features)
system.imc.core.do_dim = 16     # Columns (Output Filters)
system.imc.core.m_batches = 1
system.imc.core.num_macros = 1
system.imc.core.num_weight_banks = 2 # Enable Double Buffering
system.imc.core.bi = 8          # Input Bits
system.imc.core.bw = 8          # Weight Bits
system.imc.core.bcycles = 1     # Cycles per Bit
system.imc.core.energy_per_mac_fj = 22.88

# 4b. Connect Ports to System Bus
# PIO: CPU writes commands here
system.imc.pio = system.membus.mem_side_ports
# DMA: Accelerator reads DRAM here
system.imc.dma = system.membus.cpu_side_ports

# -------------------------------------------------------------------------
# 5. MEMORY & INTERRUPTS
# -------------------------------------------------------------------------
system.cpu.createInterruptController()

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports


system.system_port = system.membus.cpu_side_ports

# -------------------------------------------------------------------------
# 6. WORKLOAD SELECTION (Passed via command line)
# -------------------------------------------------------------------------
# -------------------------------------------------------------------------
# 5. WORKLOAD SELECTION
# -------------------------------------------------------------------------
binary = 'tests/test-progs/imc_acc_model/cpu_gemm_int8' 
if len(sys.argv) > 1:
    binary = sys.argv[1]

process = Process()
process.cmd = [binary]

# Note: We assign the process here, but we DON'T map it yet!
system.cpu.workload = process
system.workload = SEWorkload.init_compatible(binary)
system.cpu.createThreads()

# -------------------------------------------------------------------------
# 6. INSTANTIATE (The C++ Objects come alive here)
# -------------------------------------------------------------------------
root = Root(full_system=False, system=system)
m5.instantiate()

# -------------------------------------------------------------------------
# 7. MAP PAGE TABLE (Must happen AFTER instantiate)
# -------------------------------------------------------------------------
# 1. Map Accelerator Registers (Same as before)
system.cpu.workload[0].map(0x30000000, 0x30000000, 0x1000, False)

# 2. Map Shared Data Region (Inside DRAM)
# Virtual 0x10000000 -> Physical 0x10000000 (256MB mark)
# We tell the CPU: "This address exists, and it maps 1:1 to Physical RAM."
system.cpu.workload[0].map(0x10000000, 0x10000000, 0x100000, True)
# -------------------------------------------------------------------------
# 8. RUN SIMULATION
# -------------------------------------------------------------------------
print(f"--- Starting Baremetal Simulation with '{binary}' ---")
exit_event = m5.simulate()
m5.stats.dump()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")