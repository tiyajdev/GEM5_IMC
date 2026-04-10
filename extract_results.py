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
        "simSeconds",                                     # Total Runtime
        "simInsts",                                       # Total Instructions
        
        # --- Accelerator (Compute) ---
        "system.imc.core.total_ops",                
        "system.imc.core.total_energy_fj",          
        "system.imc.core.total_processing_latency", 
        
        # --- L1 Instruction Cache ---
        "system.cpu_icache.overallHits::total",           # Cache Hits (Instructions found in cache)
        "system.cpu_icache.overallMisses::total",         # Cache Misses (Instructions fetched from SRAM)
        
        # --- Memory (SRAM / SimpleMemory) ---
        "system.sram.bytesRead::total",             
        "system.sram.bytesWritten::total",          
        
        # --- Memory Breakdown ---
        "system.sram.bytesRead::cpu.inst",          
        "system.sram.bytesRead::cpu.data",          
        "system.sram.bytesWritten::cpu.data",       
        "system.sram.bytesRead::imc",               
        "system.sram.bytesWritten::imc",            
    ]

    print(f"--- Parsing: {stat_file} ---")

    # Read the file line by line
    with open(stat_file, 'r') as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            
            stat_name = parts[0]
            
            # Exact Match Check
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
    sim_insts = get_val("simInsts")
    print(f"{'Total Execution Time':<35} | {sim_sec:.6f} s")
    print(f"{'CPU Instructions Executed':<35} | {int(sim_insts)}")
    
    print("-" * 60)

    # --- 2. Accelerator Performance ---
    imc_ops = get_val("system.imc.core.total_ops")
    imc_lat_ns = get_val("system.imc.core.total_processing_latency") / 1000.0 
    imc_energy_pj = get_val("system.imc.core.total_energy_fj") / 1000.0
    
    print(f"{'IMC Total MAC Ops':<35} | {int(imc_ops)}")
    print(f"{'IMC Active Latency':<35} | {imc_lat_ns:.3f} ns")
    print(f"{'IMC Total Energy':<35} | {imc_energy_pj:.3f} pJ")

    print("-" * 60)

    # --- 3. Cache Performance & Energy ---
    # Retrieve Hits and Misses
    cache_hits = get_val("system.cpu_icache.overallHits::total")
    cache_misses = get_val("system.cpu_icache.overallMisses::total")
    
    # CACTI Energy Estimates for 16kB SRAM
    E_PER_HIT_PJ = 5.0
    E_PER_MISS_PJ = 15.0
    
    cache_energy_pj = (cache_hits * E_PER_HIT_PJ) + (cache_misses * E_PER_MISS_PJ)
    
    print(f"{'I-Cache Hits (Local)':<35} | {int(cache_hits)}")
    print(f"{'I-Cache Misses (To Bus)':<35} | {int(cache_misses)}")
    print(f"{'I-Cache Dynamic Energy':<35} | {cache_energy_pj:.3f} pJ")

    print("-" * 60)
    
    # --- 4. Memory Traffic Detailed Breakdown (SRAM) ---
    # Retrieve raw values
    cpu_inst  = get_val("system.sram.bytesRead::cpu.inst")
    cpu_d_rd  = get_val("system.sram.bytesRead::cpu.data")
    cpu_d_wr  = get_val("system.sram.bytesWritten::cpu.data")
    
    imc_rd    = get_val("system.sram.bytesRead::imc")
    imc_wr    = get_val("system.sram.bytesWritten::imc")
    
    # Calculate Total Data Traffic (Read + Write)
    total_bytes = cpu_inst + cpu_d_rd + cpu_d_wr + imc_rd + imc_wr
    
    # Calculate Percentages
    pct_cpu_inst = (cpu_inst / total_bytes * 100) if total_bytes > 0 else 0
    pct_cpu_data = ((cpu_d_rd + cpu_d_wr) / total_bytes * 100) if total_bytes > 0 else 0
    pct_imc      = ((imc_rd + imc_wr) / total_bytes * 100) if total_bytes > 0 else 0

    print(f"{'MEMORY TRAFFIC BREAKDOWN':<35} | {'BYTES':<10} | {'% TOTAL':<8}")
    print("-" * 60)
    print(f"{' [CPU] Instruction Fetch (SRAM)':<35} | {int(cpu_inst):<10} | {pct_cpu_inst:.1f}%")
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