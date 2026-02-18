import m5
from m5.objects import *
from m5.util import addToPath
import os, sys

# -------------------------------------------------------------------------
# 0. DEFINE A SIMPLE CACHE CLASS
# -------------------------------------------------------------------------
# We use 4kB to be roughly comparable to your IMC's 2.3kB buffer.
class L1Cache(Cache):
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20
    size = '4kB'  # Standard size closest to your 2304 Bytes
    
# -------------------------------------------------------------------------
# 1. SETUP SYSTEM & CLOCK
# -------------------------------------------------------------------------
system = System()
system.clk_domain = SrcClockDomain(clock='1GHz', voltage_domain=VoltageDomain())
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# -------------------------------------------------------------------------
# 2. CREATE CPU (RISC-V)
# -------------------------------------------------------------------------
system.cpu = RiscvTimingSimpleCPU()

# -------------------------------------------------------------------------
# 3. CREATE SYSTEM BUS (Crossbar)
# -------------------------------------------------------------------------
system.membus = SystemXBar()

# -------------------------------------------------------------------------
# 4. MEMORY HIERARCHY (The Baseline Replacement for IMC)
# -------------------------------------------------------------------------
# Instead of an IMC Buffer, we give the CPU standard Caches.
system.cpu.icache = L1Cache()
system.cpu.dcache = L1Cache()

# Connect CPU -> Cache
system.cpu.icache_port = system.cpu.icache.cpu_side
system.cpu.dcache_port = system.cpu.dcache.cpu_side

# Connect Cache -> Bus
system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports

# Connect System Port (for loading binaries)
system.system_port = system.membus.cpu_side_ports

# -------------------------------------------------------------------------
# 5. MEMORY CONTROLLER & DRAM
# -------------------------------------------------------------------------
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.cpu.createInterruptController()

# -------------------------------------------------------------------------
# 6. WORKLOAD SELECTION
# -------------------------------------------------------------------------
# CRITICAL: You must use a software-only version of your code here!
# If you run the IMC binary here, it will crash when trying to write to 0x30000000
binary = 'tests/test-progs/imc_acc_model/cpu_gemm_int8_SOFTWARE_ONLY' 

if len(sys.argv) > 1:
    binary = sys.argv[1]

process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.workload = SEWorkload.init_compatible(binary)
system.cpu.createThreads()

# -------------------------------------------------------------------------
# 7. INSTANTIATE
# -------------------------------------------------------------------------
root = Root(full_system=False, system=system)
m5.instantiate()

# -------------------------------------------------------------------------
# 8. MAP PAGE TABLE
# -------------------------------------------------------------------------
# NOTE: We REMOVED the 0x30000000 mapping because the IMC is gone.

# Map Shared Data Region (If your software still puts data here)
# Virtual 0x10000000 -> Physical 0x10000000
system.cpu.workload[0].map(0x10000000, 0x10000000, 0x100000, True)

# -------------------------------------------------------------------------
# 9. RUN SIMULATION
# -------------------------------------------------------------------------
print(f"--- Starting Baseline Simulation (CPU+Cache) with '{binary}' ---")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")