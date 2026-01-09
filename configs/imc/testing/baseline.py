import os
import m5
from m5.objects import *

system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MB")]

system.cpu = TimingSimpleCPU()
system.membus = SystemXBar(width=64)

system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR4_2400_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# --- NOTICE: NO DIGITAL IMC HARDWARE HERE ---

# Use the SOFTWARE-ONLY version of your MNIST binary
# (The one that uses loops instead of /dev/imc)
binary = os.path.abspath("workloads/mnist/bin/mnist_sw_baseline")
system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary]
# --- NOTICE: NO DRIVER ATTACHED ---

system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()
exit_event = m5.simulate()