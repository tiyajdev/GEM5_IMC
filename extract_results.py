import sys
import os

def parse_gem5_stats(stat_file):
    if not os.path.exists(stat_file):
        print(f"Error: File '{stat_file}' not found.")
        return

    # Dictionary to store the values we find
    data = {}
    
    # ------------------------------------------------------------------------
    # DEFINITIONS: The specific stats we care about
    # ------------------------------------------------------------------------
    target_stats = [
        # --- Time & CPU ---
        "simSeconds",                               # Total Runtime
        "system.cpu.commitStats0.numInsts",         # CPU Instructions
        
        # --- Accelerator (Compute) ---
        "system.imc.core.total_ops",                # Accelerator Workload
        "system.imc.core.total_energy_fj",          # Accelerator Energy
        "system.imc.core.total_processing_latency", # Accelerator Active Time
        
        # --- Memory (High Level) ---
        "system.mem_ctrl.dram.bwTotal::total",      # Avg Bandwidth
        "system.mem_ctrl.dram.bytesRead::total",    # Grand Total Read
        "system.mem_ctrl.dram.bytesWritten::total", # Grand Total Written
        
        # --- Memory (Detailed Breakdown) ---
        "system.mem_ctrl.dram.bytesInstRead::total", # CPU Instruction Fetch
        "system.mem_ctrl.dram.bytesRead::cpu.data",  # CPU Data Read (Stack/Heap)
        "system.mem_ctrl.dram.bytesWritten::cpu.data", # CPU Data Write
        "system.mem_ctrl.dram.bytesRead::imc",       # Accelerator DMA Read
        "system.mem_ctrl.dram.bytesWritten::imc"     # Accelerator DMA Write
    ]

    print(f"--- Parsing: {stat_file} ---")

    # Read the file line by line
    with open(stat_file, 'r') as f:
        for line in f:
            # Skip empty lines or comments
            if not line.strip() or line.startswith("---"):
                continue
            
            parts = line.split()
            stat_name = parts[0]
            
            # Helper to check if line starts with target (handles minor version diffs)
            if stat_name in target_stats:
                try:
                    data[stat_name] = float(parts[1])
                except ValueError:
                    continue

    # ------------------------------------------------------------------------
    # REPORT GENERATION
    # ------------------------------------------------------------------------
    
    # Helper to safely get data
    def get_val(key):
        return data.get(key, 0.0)

    print("\n" + "="*60)
    print(f"{'METRIC':<35} | {'VALUE':<20}")
    print("="*60)

    # --- 1. System Performance ---
    sim_sec = get_val("simSeconds")
    print(f"{'Total Execution Time':<35} | {sim_sec:.6f} s")
    print(f"{'CPU Instructions Executed':<35} | {int(get_val('system.cpu.commitStats0.numInsts'))}")
    
    print("-" * 60)

    # --- 2. Accelerator Performance ---
    imc_ops = get_val("system.imc.core.total_ops")
    imc_lat_ns = get_val("system.imc.core.total_processing_latency") / 1000.0
    imc_energy_pj = get_val("system.imc.core.total_energy_fj") / 1000.0
    
    print(f"{'IMC Total MAC Ops':<35} | {int(imc_ops)}")
    print(f"{'IMC Active Latency':<35} | {imc_lat_ns:.3f} ns")
    print(f"{'IMC Total Energy':<35} | {imc_energy_pj:.3f} pJ")

    print("-" * 60)
    
    # --- 3. Memory Traffic Detailed Breakdown ---
    # Retrieve raw values
    cpu_inst  = get_val("system.mem_ctrl.dram.bytesInstRead::total")
    cpu_d_rd  = get_val("system.mem_ctrl.dram.bytesRead::cpu.data")
    cpu_d_wr  = get_val("system.mem_ctrl.dram.bytesWritten::cpu.data")
    imc_rd    = get_val("system.mem_ctrl.dram.bytesRead::imc")
    imc_wr    = get_val("system.mem_ctrl.dram.bytesWritten::imc")
    
    total_bytes = cpu_inst + cpu_d_rd + cpu_d_wr + imc_rd + imc_wr
    
    # Calculate Percentages
    pct_cpu_inst = (cpu_inst / total_bytes * 100) if total_bytes > 0 else 0
    pct_cpu_data = ((cpu_d_rd + cpu_d_wr) / total_bytes * 100) if total_bytes > 0 else 0
    pct_imc      = ((imc_rd + imc_wr) / total_bytes * 100) if total_bytes > 0 else 0

    print(f"{'MEMORY TRAFFIC BREAKDOWN':<35} | {'BYTES':<10} | {'% TOTAL':<8}")
    print("-" * 60)
    print(f"{' [CPU] Instruction Fetch':<35} | {int(cpu_inst):<10} | {pct_cpu_inst:.1f}%")
    print(f"{' [CPU] Data Read':<35} | {int(cpu_d_rd):<10} |")
    print(f"{' [CPU] Data Write':<35} | {int(cpu_d_wr):<10} | {pct_cpu_data:.1f}% (Combined)")
    print(f"{' [IMC] DMA Read':<35} | {int(imc_rd):<10} |")
    print(f"{' [IMC] DMA Write':<35} | {int(imc_wr):<10} | {pct_imc:.1f}% (Combined)")
    print("-" * 60)
    print(f"{'TOTAL DATA MOVED':<35} | {int(total_bytes):<10} | 100.0%")
    print("="*60 + "\n")

if __name__ == "__main__":
    path = "m5out/stats.txt"
    if len(sys.argv) > 1:
        path = sys.argv[1]
    
    parse_gem5_stats(path)