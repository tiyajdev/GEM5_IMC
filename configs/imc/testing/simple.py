import m5
from m5.objects import *

# 1. Create the System
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# 2. Memory Mode (Timing is fine, or Atomic for speed)
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# 3. Create CPU
# Note: Changing this to X86TimingSimpleCPU if using build/X86
system.cpu = RiscvTimingSimpleCPU() 

# 4. Create Bus
system.membus = SystemXBar()

# 5. Connect CPU to Bus
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()

# 6. Connect Memory
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# 7. Workload
binary = 'workloads/testing/hello'
system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

# 8. Run
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")